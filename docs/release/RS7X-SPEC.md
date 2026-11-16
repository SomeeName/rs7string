# RS7X: the SNG extension block for extended-range charts

**Byte-level specification of the block that carries chord data for strings beyond the stock six.**

[Contributing](../../CONTRIBUTING.md) · [Development](../DEVELOPMENT.md) · [Architecture](../ARCHITECTURE.md) · **RS7X spec** · [Chart tools](CHART-TOOLS.md) · [Releasing](../RELEASING.md) · [Agents](../../AGENTS.md)

| | |
|---|---|
| Format version | 1 (2026-10-13) |
| Document revision | 2026-11-06: test results section removed, checklist generalised. 2026-11-01: section masks for bass, and the game derives missing mask bits; corrected 8-string example. Byte layout unchanged since 2026-10-14 |
| Tracking issue | SomeeName/rs7string#18 |
| Audience | Chart tools (DotCharter and others) exporting 7- and 8-string guitar and 5/6-string bass arrangements for Rocksmith 2014 Remastered with the rs7string mod |
| Reference writer | The maintainer's rs7chart tool (to be published later: `src/rs7chart/sng.py` `ExtBlock`, writer in `convert.py`, tests in `tests/test_extblock.py`) |
| Reference reader | rs7string `src/model/extblock.cpp`, `rs7ext::Parse` |

> [!TIP]
> Chart-tool authors: start with the [chart tools guide](CHART-TOOLS.md) (overview, arrangement XML, XML to
> SNG, checklists). [Section 8](#8-writer-checklist)
> is the export checklist, and [section 4](#4-writer-rules) has the writer rules.
> Chart authors who only need the basics should read the [charting guide](CHARTING.md).

## Contents

1. [Why a block, and where it goes](#1-why-a-block-and-where-it-goes)
2. [Byte layout](#2-byte-layout): [header](#21-header-16-bytes), [section](#22-section-12-byte-header--payload),
   [`STRS`](#23-strs---strings-version-1-critical-must-be-the-first-section),
   [`CHDT`](#24-chdt---chord-templates-version-1-critical),
   [`CHNT`](#25-chnt---chord-notes-version-1-not-critical), [future sections](#26-future-sections)
3. [Validation rules](#3-validation-rules)
4. [Writer rules](#4-writer-rules)
5. [Rules for every extended chart](#5-rules-for-every-extended-chart-not-only-the-block)
6. [How rs7string reads it](#6-how-rs7string-reads-it)
7. [Examples](#7-examples)
8. [Writer checklist](#8-writer-checklist)

## 1. Why a block, and where it goes

The stock SNG has a fixed six-slot layout for chords: a chord template (0x48 bytes) holds
`frets[6]`, `fingers[6]`, `notes[6]`, and a ChordNotes record (0x948 bytes) holds six per-string
technique slots. There is no instrument string count in the file either (bass uses the same six
slots with strings 4/5 empty). Single notes are fine (the note's `string` byte can be 6 or 7);
everything per string inside a chord beyond string 5 needs another home. RS7X is that home: one
block appended to the SNG body after the last stock section.

**Placement.** Inside the zlib-compressed SNG body, immediately after the MetaData section (the
last stock section, which ends with `maxDifficulty`). The container's "uncompressed size" field
counts the block:

    plain SNG file  = u32 0x4A | u32 flags 1 | 16 zero bytes | u32 bodySize | zlib(body) | 56 zero bytes
    body            = Beats .. Levels | MetaData | RS7X block            (bodySize = all of it)

**Why the stock game is fine with it** (September 2022 exe, `SNG_Song_Load` 0x558BD0):

- Checked in the game code: the loader reads the body size from the container, inflates with zlib's
  `uncompress` (0xE316E0: `inflateInit_`, `inflate(Z_FINISH)`, `*destLen = total_out`) into a
  buffer of exactly that size and fails only if `uncompress` fails or the inflated length differs
  from the size field (0x558DBD / 0x558DC9). It then reads the sections from a memory stream
  (vtable 0x11E7C64; `+10h` buffer, `+14h` position, `+18h` size; reads clamp at the end) and, after
  the last MetaData read (0x55922A), destroys the stream and returns success (0x55922C..0x559246).
  The position is never compared with the size, so bytes after MetaData are never read.
- Checked in game: packages with the block load and play; rs7string's XB1 hook logs
  the stream position (end of MetaData) below the body size on every such load.
- Consequence: the block must be **inside** the zlib stream. Data after the zlib stream (before the
  56-byte signature slot) is a different matter and is not used. A body larger than the size field
  makes `uncompress` fail (Z_BUF_ERROR) and the song does not load.

Other readers: Not verified: Rocksmith2014.NET-style readers parse section by section
and stop after MetaData, so they ignore the block. `tools/rs7chart` reads and validates it
(`rs7chart dump` prints it and flags rule violations).

## 2. Byte layout

All integers little-endian, no alignment or padding anywhere. `i8`/`u8` are bytes, `i16`/`u16`
2 bytes, `u32` 4 bytes, `f32` IEEE single.

The whole format at a glance (C-like notation; `[n]` = n entries back to back, nothing in between). The
tables below give every field's meaning, [section 7](#7-examples) walks through real blocks byte by byte.

```c
Block     { Header header; Section sections[header.sectionCount]; }
Header    { char magic[4] = "RS7X"; u16 version = 1; u16 headerSize = 16; u32 blockSize; u32 sectionCount; }
Section   { char id[4]; u16 version = 1; u16 flags; /* bit 0 = critical */ u32 size; u8 payload[size]; }

// payload of each section id
STRS      { u8 instrument; u8 stringCount; u8 columnCount; u8 reserved = 0; u8 strings[columnCount]; }
CHDT      { u32 templateCount; Fret frets[templateCount][columnCount]; }
CHNT      { u32 recordCount;   Tech techs[recordCount][columnCount]; }      // Tech has a variable size

Fret      { i8 fret; i8 finger; }                                           // -1 = not in the chord / none
Tech      { u32 mask; i8 slideTo; i8 slideUnpitchTo; i16 vibrato; u8 bendCount; Bend bends[bendCount]; }
Bend      { f32 time; f32 step; u32 reserved = 0; }
```

### 2.1 Header (16 bytes)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | char[4] | magic | `RS7X` (`52 53 37 58`) |
| 4 | u16 | version | 1 |
| 6 | u16 | headerSize | 16 (readers skip any extra header bytes up to headerSize) |
| 8 | u32 | blockSize | bytes from the magic to the end of the last section |
| 12 | u32 | sectionCount | number of sections that follow |

### 2.2 Section (12-byte header + payload)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | char[4] | id | e.g. `STRS` |
| 4 | u16 | version | 1 for every section defined here |
| 6 | u16 | flags | bit 0 = critical; other bits 0 |
| 8 | u32 | size | payload bytes after this header |

A reader that does not know a section's id, or knows the id but not its version, skips `size`
bytes if the section is not critical, and ignores the **whole block** if it is critical. Every
section appears at most once.

### 2.3 `STRS` - strings (version 1, critical, must be the first section)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | u8 | instrument | 0 = guitar, 1 = extended bass (5 / 6 strings, see below) |
| 1 | u8 | stringCount | all strings of the instrument: 7 for a 7-string, 8 for an 8-string (the format allows up to 16; this release plays 7 and 8) |
| 2 | u8 | columnCount | number of extended columns = stringCount - 6 for guitar |
| 3 | u8 | reserved | 0 |
| 4 | u8[columnCount] | strings | the string index of each column: guitar 6, 7, ... stringCount-1 |

Payload size = 4 + columnCount. "Column" below means one of these extended strings, in this order.

Extended bass (instrument 1): stringCount 5 (low B) or 6 (low B and high C), columnCount 0, no strings, MetaData
tuning has six entries. The extra strings use the stock six-slot data: string index 4 = B0 (MIDI 23 + tuning[4]),
5 = C3 (MIDI 48 + tuning[5]); strings 0-3 stay E A D G. Chords too: chord template columns 4 / 5 hold the low B / high
C (checked, bass chords with string 4 and 5 scored 20/20 in the 2026-10-30 low-tuning runs). The block only marks the arrangement (a six-entry tuning
alone cannot tell it from a stock padded four-string bass). Bass blocks must omit `CHDT` and `CHNT`, even with
zero records: the reader rejects these sections with zero columns. The manifest and .hsan
`Tuning` object get `Rs7String4` (low B) and, for six strings, `Rs7String5` (high C) after `string5`: rs7string builds
the bass with as many strings as these keys say.
String indices: 0 = low E ... 5 = high e (stock); 6 = the 7th string (B1, MIDI 35); 7 = the 8th
string (F#1, MIDI 30).

### 2.4 `CHDT` - chord templates (version 1, critical)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | templateCount: must equal the stock Chords section's count |
| 4 | records | templateCount x columnCount x { i8 fret, i8 finger } |

Record `t` belongs to stock chord template `t` (the note's `chordId`), columns in STRS order.
`fret` -1 = the string is not part of this chord, 0 = open, 1..24 fretted. `finger` -1 = none,
0 = thumb, 1..4 = index..pinky (the stock template's convention). Payload size =
4 + 2 x templateCount x columnCount. The expected pitch is not stored: it is
`openMidi[string] + tuning[string] + fret` (MetaData tuning, open MIDI 35 for string 6, 30 for 7),
the same formula the six stock `notes[]` follow.

### 2.5 `CHNT` - chord notes (version 1, not critical)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | recordCount: must equal the stock ChordNotes section's count |
| 4 | entries | recordCount x columnCount x Tech |

Tech (9 + 12 x bendCount bytes), the per-string fields of the stock 0x948 ChordNotes record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | mask: note mask bits as in the stock record's `mask[s]` (0x2000 sustain, 0x4 open, 0x800 slide, 0x1000 bend, 0x10000 vibrato, 0x400000 unpitched slide, ...) |
| 4 | i8 | slideTo: target fret, -1 = none |
| 5 | i8 | slideUnpitchTo: target fret, -1 = none |
| 6 | i16 | vibrato: as stock `vibrato[s]` (0 = none) |
| 8 | u8 | bendCount: 0..32 (the stock BendData32 `usedCount`) |
| 9 | BendValue[bendCount] | { f32 time (song seconds), f32 step (1 = half step), u32 reserved 0 }: the used entries of the stock BendData32, 12 bytes each like stock |

Record `r` belongs to stock ChordNotes record `r` (the note's `chordNotesId`). A string that is not
in the chord, or has no technique, gets the empty entry `00 00 00 00 ff ff 00 00 00`.

### 2.6 Future sections

New data goes into new sections (or a new section version); readers skip what they do not know
(non-critical) or drop the block (critical). Candidates: per-level data, handshape/arpeggio
extensions. Extended bass needs none: its extra strings use the stock columns 4 / 5.

## 3. Validation rules

A reader treats the block as absent (and falls back, see 5) when any rule fails:

1. The block starts at the first byte after MetaData; `blockSize` equals the number of body bytes
   after MetaData (nothing may follow the block). Bytes after MetaData that do not start with
   `RS7X` are not a block.
2. `version` is 1 (others: not understood, ignore); 16 <= `headerSize` <= `blockSize`.
3. The sections tile the block exactly: `sectionCount` headers, each payload inside the block, the
   last one ending at `blockSize`.
4. The first section is `STRS`; no id twice; an unknown or unknown-version section with the
   critical bit drops the block.
5. `STRS`: instrument 1 (bass): stringCount 5 or 6, columnCount 0, six MetaData tuning entries; omit `CHDT` and
   `CHNT`. Instrument 0: 7 <= stringCount <= 16; **stringCount == the number of MetaData tuning
   entries**; columnCount == stringCount - 6; strings == 6, 7, ..., stringCount - 1.
6. `CHDT`: templateCount == stock chord template count; payload exactly 4 + 2 x count x columns;
   fret in {-1, 0..24}; finger in {-1, 0..4}; fret -1 implies finger -1.
7. `CHNT`: recordCount == stock ChordNotes count; bendCount <= 32; slideTo and slideUnpitchTo in
   {-1, 0..24}; the entries consume the payload exactly.
8. Each section's payload is consumed exactly (no trailing bytes inside a section).
9. rs7string also ignores a block larger than 16 MiB.

Absent `CHDT` = every extended column is -1. Absent `CHNT` = no extended techniques.

## 4. Writer rules

- **When:** write the block for every guitar arrangement with more than six strings (MetaData tuning
  has 7 or 8 entries), with `STRS`, `CHDT` and `CHNT` in this order, even when the counts are 0, and
  for every five- or six-string bass (`STRS` instrument 1, [section 2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)).
  Bass blocks must omit `CHDT` and `CHNT`, even with zero records. Write no block for 6-string guitar and
  4-string bass: their SNG stays byte-for-byte stock.
- **Chord templates:** the six stock columns stay a valid six-column projection (strings 0..5:
  frets/fingers as usual, unused = -1, MIDI `notes[s]` = -1 for unused). Never put an extended
  string's pitch into a free stock MIDI slot. A template keeps at least one stock column (one-column
  templates such as C#5 = s6 fret 2 + s0 fret 4 -> stock column s0 only: checked to load, render
  and score with rs7string, M4). The extended columns go to `CHDT`; deduplicate templates on all
  columns (two chords with the same stock columns but different string-6 frets are two templates).
- **name[31]:** leave the last byte of the 32-byte template name 0 (the pre-block M4 format put the
  string-6 column there; rs7string still reads it for old packages, a valid block overrides it).
  Names: printable ASCII, at most 30 characters.
- **Chord notes:** create a ChordNotes record (and set the note's `chordNotesId` and the chord
  note's CHORD_NOTES mask bit 0x1000000) when any string, stock or extended, has technique data
  **or is open** (fret 0: mask OPEN 0x4), exactly as stock tools do for open stock strings;
  deduplicate records on stock + extended data. The extended strings' data goes to the `CHNT` entry
  of that record. The game draws an open string's chord gem as the open-string bar only from that
  OPEN bit (chord spawner 0x7A0950); with rs7string's native N-slot chords (stage 2) the 7th string
  runs through the same code, so an open extended string without a record is drawn as a fretted gem.
  (Changed 2026-10-13: version 1 packages written before this rule have no record for chords whose
  only open string is extended.)
- **Double stop:** a chord with exactly two sounding strings, stock and extended counted together,
  gets the note's DOUBLE_STOP bit 0x02000000 (rs7string recounts it over all 8 strings at load anyway).
- **Single notes** on extended strings are stock note records with `string` = 6 (7); no block data.
- **Sections:** `StringMask` bit 6 (bit 7 for the 8th string) set in sections that use the string,
  for chords too. Bass: bits 4 / 5 as for any stock string. A missing bit is not an error: when the
  song loads, the game ORs `1 << string` into each section's mask for every single note and chord
  fret and then fills it in for earlier sections (checked, `SNG_Song_PostProcessNotes`, chord frets
  `0x55CC1C`, single notes `0x55CC71`, fill-in `0x55CD30`). Stock code covers strings 0-5, so bass
  bits 4 / 5 come for free; rs7string adds guitar bits 6 / 7 (`Rs7_Mid_N12`, `N13`, `TP11`). A
  bit the file sets but no note uses is kept (not verified: no clearing pass seen), so write exact masks.

## 5. Rules for every extended chart (not only the block)

| What | Rule | Status |
|---|---|---|
| String index | the 7th string is index 6 (low B, MIDI 35 + tuning[6]); single notes use `string` = 6 | checked |
| SNG tuning | MetaData tuning has **7** entries for a 7-string guitar (index 6 = offset of the B), 8 for an 8-string | checked |
| Manifest key | the arrangement manifest's and the .hsan's `Tuning` object gets `Rs7String6` (the 7th string's offset) after `string5`; **never `string6`** (the stock game's song list reads every `string%d` key into room for six values, so a `string6` key crashes it) | checked |
| Chord data | stock six columns + RS7X block (this document); old packages: name[31] | checked |
| Package | plain PSARC (unencrypted TOC, archive flags without 0x4; SNG flags 1 = zlib, no encryption, zero IV and signature slots), loaded by rs7string's `[psarc] plain` setting (or a CDLC enabler that loads plain packages); any CDLC enabler still bypasses the signature check. No Ubisoft keys anywhere | checked |
| Text | DLC keys, file names, chord/section/phrase names: printable ASCII | project rule |
| 8 strings | index 7 = F#1 (MIDI 30 + tuning[7]), 8 tuning entries, `Rs7String6` + `Rs7String7`; STRS stringCount 8, columns 6, 7. rs7string plays 8 strings: notes, chords, eight-peg headstock, tuner | supported |
| Bass 5/6 | STRS instrument 1, stringCount 5 / 6, no columns ([2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)), Tuning keys `Rs7String4` / `Rs7String5`; rs7string builds a 5 / 6-string bass and plays single notes and chords on strings 4 / 5 | supported |

## 6. How rs7string reads it

`src/model/extblock.cpp` (parser) and `src/features/nslot/hooks_extblock.cpp` (store), hook XB1 at 0x55922C (end of `SNG_Song_Load`, loader thread, before the
body is freed): parses and validates the bytes between the stream position and its size against
the loaded song (template count `+94h`, ChordNotes count `+0ACh`, tuning `+110h`) and keeps the
parsed block per song. Log: one `extblock: SNG ...` line per loaded SNG (block summary, or the
reason it was ignored).

Stage 2 (native N-slot chords, `src/features/nslot/`, docs/ARCHITECTURE.md): the
song's chord templates and ChordNotes records are converted into superset records (the stock record
followed by 8-slot arrays: template frets/fingers/MIDI, ChordNotes masks/slides/vibrato/bends) once
at load (`CHDT` -> template slots 6/7, `CHNT` -> ChordNotes slots 6/7), and the game's own chord code
reads those. Packages without a block: the old name[31] byte fills slot 6 (7-string arrangements).
Stage 1 rewrote name[31] from the block instead; that is gone. Since 1.0 the game's chord bend, vibrato, slide and sustain loops also run over slots
6/7, so the `CHNT` entries are used, not only parsed.

## 7. Examples

Three complete blocks, one row per field. Offsets count from the first byte of the block (the `R` of `RS7X`), in
decimal. The one-line hex under each table is the same block for copying into a test.

### 7.1 Seven-string guitar (78 bytes)

Two chord templates: template 0 is a C#5 with the 7th string at fret 2 (index finger), template 1 does not use the
7th string. One ChordNotes record, whose 7th-string entry is sustained.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| **0** | | **Header** | |
| 0 | `52 53 37 58` | magic | `RS7X` |
| 4 | `01 00` | version | 1 |
| 6 | `10 00` | headerSize | 16 |
| 8 | `4e 00 00 00` | blockSize | 78 |
| 12 | `03 00 00 00` | sectionCount | 3 |
| **16** | | **Section `STRS`** | |
| 16 | `53 54 52 53` | id | `STRS` |
| 20 | `01 00` | version | 1 |
| 22 | `01 00` | flags | critical |
| 24 | `05 00 00 00` | size | 5 payload bytes follow |
| 28 | `00` | instrument | 0 = guitar |
| 29 | `07` | stringCount | 7 |
| 30 | `01` | columnCount | 1 extended column |
| 31 | `00` | reserved | 0 |
| 32 | `06` | strings[0] | column 0 is string 6 (the low B) |
| **33** | | **Section `CHDT`** | |
| 33 | `43 48 44 54` | id | `CHDT` |
| 37 | `01 00` | version | 1 |
| 39 | `01 00` | flags | critical |
| 41 | `08 00 00 00` | size | 8 |
| 45 | `02 00 00 00` | templateCount | 2 (= the stock Chords count) |
| 49 | `02 01` | template 0, column 0 | fret 2, finger 1 (index) |
| 51 | `ff ff` | template 1, column 0 | -1, -1: string 6 not in this chord |
| **53** | | **Section `CHNT`** | |
| 53 | `43 48 4e 54` | id | `CHNT` |
| 57 | `01 00` | version | 1 |
| 59 | `00 00` | flags | not critical |
| 61 | `0d 00 00 00` | size | 13 |
| 65 | `01 00 00 00` | recordCount | 1 (= the stock ChordNotes count) |
| 69 | `00 20 00 00` | record 0, column 0: mask | 0x2000 = sustain |
| 73 | `ff` | slideTo | -1 = no slide |
| 74 | `ff` | slideUnpitchTo | -1 = no unpitched slide |
| 75 | `00 00` | vibrato | 0 = none |
| 77 | `00` | bendCount | 0, so no Bend entries; the block ends at 78 |

    52 53 37 58 01 00 10 00 4e 00 00 00 03 00 00 00 53 54 52 53 01 00 01 00 05 00 00 00 00 07 01 00 06
    43 48 44 54 01 00 01 00 08 00 00 00 02 00 00 00 02 01 ff ff
    43 48 4e 54 01 00 00 00 0d 00 00 00 01 00 00 00 00 20 00 00 ff ff 00 00 00

### 7.2 Eight-string guitar (88 bytes)

One chord template, an F#5: 8th string open, 7th string at fret 2 (middle finger); its stock column s0 (fret 2) is
in the stock template as usual. The open 8th string needs its `OPEN` bit, so the chord gets a ChordNotes record
([section 4](#4-writer-rules)). With two columns, every template and record has two entries: column 0 =
string 6, column 1 = string 7. Section headers are shown as one row.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| 0 | `52 53 37 58 01 00 10 00 58 00 00 00 03 00 00 00` | Header | `RS7X`, version 1, header 16, block 88, 3 sections |
| 16 | `53 54 52 53 01 00 01 00 06 00 00 00` | `STRS` header | version 1, critical, 6 bytes |
| 28 | `00 08 02 00` | instrument, stringCount, columnCount, reserved | guitar, 8 strings, 2 columns |
| 32 | `06 07` | strings | column 0 = string 6, column 1 = string 7 |
| 34 | `43 48 44 54 01 00 01 00 08 00 00 00` | `CHDT` header | version 1, critical, 8 bytes |
| 46 | `01 00 00 00` | templateCount | 1 |
| 50 | `02 02` | template 0, string 6 | fret 2, finger 2 (middle) |
| 52 | `00 ff` | template 0, string 7 | fret 0 (open), no finger |
| 54 | `43 48 4e 54 01 00 00 00 16 00 00 00` | `CHNT` header | version 1, not critical, 22 bytes |
| 66 | `01 00 00 00` | recordCount | 1 |
| 70 | `00 00 00 00 ff ff 00 00 00` | record 0, string 6 | no technique (the empty entry) |
| 79 | `04 00 00 00 ff ff 00 00 00` | record 0, string 7 | mask 0x4 = open; no slide, vibrato or bends |

    52 53 37 58 01 00 10 00 58 00 00 00 03 00 00 00 53 54 52 53 01 00 01 00 06 00 00 00 00 08 02 00 06 07
    43 48 44 54 01 00 01 00 08 00 00 00 01 00 00 00 02 02 00 ff
    43 48 4e 54 01 00 00 00 16 00 00 00 01 00 00 00 00 00 00 00 ff ff 00 00 00 04 00 00 00 ff ff 00 00 00

### 7.3 Five-string bass (32 bytes)

Only `STRS`, to mark the arrangement as an extended bass: no columns, because the low B's notes and chord frets use
the stock string 4 ([section 2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)). A six-string
bass is the same with stringCount `06`.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| 0 | `52 53 37 58 01 00 10 00 20 00 00 00 01 00 00 00` | Header | `RS7X`, version 1, header 16, block 32, 1 section |
| 16 | `53 54 52 53 01 00 01 00 04 00 00 00` | `STRS` header | version 1, critical, 4 bytes |
| 28 | `01 05 00 00` | instrument, stringCount, columnCount, reserved | extended bass, 5 strings, no columns |

    52 53 37 58 01 00 10 00 20 00 00 00 01 00 00 00 53 54 52 53 01 00 01 00 04 00 00 00 01 05 00 00

## 8. Writer checklist

What a chart tool needs to export 7/8-string guitar and 5/6-string bass arrangements for rs7string. DotCharter
follows this list.

1. **Validator:** allow string indices 6-7 for 7/8-string guitar arrangements and 4-5 for 5/6-string bass;
   keep the error beyond that.
2. **MetaData tuning:** guitar: 7 or 8 entries (index 6 = the B offset, 7 = the F# offset), not a fixed
   `short[6]`. Bass: six entries as stock (index 4 = the low B offset, 5 = the high C offset, [section 2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)).
3. **Notes:** single notes on the extra strings = stock note records with `string` = 6 / 7 (guitar) or
   4 / 5 (bass); section `StringMask` bit 6 / 7 (guitar) or 4 / 5 (bass) where the string is used
   (chords too). The game derives missing bits at load ([section 4](#4-writer-rules)).
4. **Chord templates (guitar):** stock six columns as a valid projection ([section 4](#4-writer-rules)); the extra strings'
   fret/finger go to `CHDT`; deduplicate on all columns; `name[31]` = 0; ASCII names <= 30 chars; stock
   MIDI `notes[]` only for strings 0..5 (`MapToMidiNotes` stays six-wide, never put the B or F# into a
   free slot).
5. **ChordNotes (guitar):** create a record when any string (stock or extra) has a technique or is open;
   the extra strings' mask/slide/vibrato/bends go to `CHNT`; empty entry `00 00 00 00 ff ff 00 00 00`.
6. **Double stop:** count the extra strings when setting 0x02000000.
7. **Block:** append RS7X after MetaData **inside** the zlib body; the container's body size includes it.
   Guitar: `STRS` (instrument 0, stringCount 7 / 8), `CHDT`, `CHNT`, in that order, even with zero
   records. Bass: `STRS` instrument 1, stringCount 5 / 6, columnCount 0; omit `CHDT` and `CHNT`. No block for 6-string guitar
   and 4-string bass.
8. **Manifest + HSAN:** guitar `Tuning` gets `Rs7String6` (and `Rs7String7` for 8 strings) after
   `string5`; bass gets `Rs7String4` (low B) and, for six strings, `Rs7String5` (high C). Never a key
   named `string6` or higher.
9. **Package:** plain PSARC, built without Ubisoft keys, ASCII DLC keys and file names.
10. **Import (round trip):** the tool's SNG reader should parse the block ([section 3](#3-validation-rules) rules) so an
    extended package opens with its extra strings; on a rule failure fall back to name[31] (old 7-string
    packages) and else to the stock six / four strings.
11. **Test:** play the exported chart in the game. The `extblock: SNG ...` line in `rs7string.log` must say the
    block was read (not "ignored").
