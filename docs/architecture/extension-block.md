# Internal extension block

**Developer reference for the extension block produced while loading native charts.**

The extension block is an internal representation, not an on-disk chart format. Chart writers use the
[native format](../NATIVE-FORMAT.md) (`PSXR`, `SNXR`, `ExtendedRange` and positional `Tuning`).
CONFIRMED (source): `src/model/nativesng.cpp` writes the `RSXR` magic, and `src/model/extblock.cpp` reads it.

[Contributing](../../CONTRIBUTING.md) · [Development](../DEVELOPMENT.md) · [Architecture](../ARCHITECTURE.md) · **Extension block** · [Chart tools](../release/CHART-TOOLS.md) · [Releasing](../RELEASING.md) · [Agents](../../AGENTS.md)

| | |
|---|---|
| Format version | 1 (2026-10-13) |
| Document revision | 2026-11-17: internal names and developer scope; magic `RSXR` |
| Tracking issue | SomeeName/rs-extended-range#18 |
| Audience | Developers maintaining the native loader, N-slot records and tests |
| Reference writer | `src/model/nativesng.cpp`, `rsxrnative::Convert` |
| Reference reader | RS Extended Range `src/model/extblock.cpp`, `rsxrext::Parse` |

## Contents

1. [Why a block, and where it goes](#1-why-a-block-and-where-it-goes)
2. [Byte layout](#2-byte-layout): [header](#21-header-16-bytes), [section](#22-section-12-byte-header--payload),
   [`STRS`](#23-strs---strings-version-1-critical-must-be-the-first-section),
   [`CHDT`](#24-chdt---chord-templates-version-1-critical),
   [`CHNT`](#25-chnt---chord-notes-version-1-not-critical), [future sections](#26-future-sections)
3. [Validation rules](#3-validation-rules)
4. [Writer rules](#4-writer-rules)
5. [Rules for every extended chart](#5-rules-for-every-extended-chart-not-only-the-block)
6. [How RS Extended Range reads it](#6-how-rs-extended-range-reads-it)
7. [Examples](#7-examples)
8. [Writer checklist](#8-writer-checklist)

## 1. Why a block, and where it goes

The stock SNG has a fixed six-slot layout for chords. A chord template (0x48 bytes) holds `frets[6]`,
`fingers[6]` and `notes[6]`, and a ChordNotes record (0x948 bytes) holds six per-string technique slots. The
file has no instrument string count either. Bass uses the same six slots, with strings 4/5 empty. Single notes
are fine, because the note's `string` byte can be 6 or 7. But all per-string chord data beyond string 5 needs
another home. The extension block is that home. It is one block appended to the SNG body after the last stock
section.

**Placement.** The block sits inside the zlib-compressed SNG body, immediately after the MetaData section.
MetaData is the last stock section and ends with `maxDifficulty`. The container's "uncompressed size" field
counts the block.

    plain SNG file  = u32 0x4A | u32 flags 1 | 16 zero bytes | u32 bodySize | zlib(body) | 56 zero bytes
    body            = Beats .. Levels | MetaData | extension block            (bodySize = all of it)

**Why the stock game is fine with it** (September 2022 exe, `SNG_Song_Load` 0x558BD0):

- Checked in the game code: the loader reads the body size from the container. It inflates with zlib's
  `uncompress` (0xE316E0: `inflateInit_`, `inflate(Z_FINISH)`, `*destLen = total_out`) into a buffer of
  exactly that size. It fails only if `uncompress` fails or if the inflated length differs from the size
  field (0x558DBD / 0x558DC9). It then reads the sections from a memory stream (vtable 0x11E7C64; `+10h`
  buffer, `+14h` position, `+18h` size; reads clamp at the end). After the last MetaData read (0x55922A),
  it destroys the stream and returns success (0x55922C..0x559246). The position is never compared with
  the size, so bytes after MetaData are never read.
- Checked in game: packages with the block load and play. RS Extended Range's XB1 hook logs the stream
  position (end of MetaData) below the body size on every such load.
- As a result, the block must be **inside** the zlib stream. Data after the zlib stream (before the
  56-byte signature slot) is a different matter and is not used. A body larger than the size field makes
  `uncompress` fail (Z_BUF_ERROR), and the song does not load.

The block exists only in the loader buffer after native conversion. Package readers consume the native body.

## 2. Byte layout

All integers are little-endian, and there is no alignment or padding anywhere. `i8`/`u8` are bytes,
`i16`/`u16` are 2 bytes, `u32` is 4 bytes and `f32` is an IEEE single.

The block below shows the whole format in C-like notation, where `[n]` means n entries back to back with
nothing in between. The tables after it give the meaning of every field, and [section 7](#7-examples) walks
through real blocks byte by byte.

```c
Block     { Header header; Section sections[header.sectionCount]; }
Header    { char magic[4] = "RSXR"; u16 version = 1; u16 headerSize = 16; u32 blockSize; u32 sectionCount; }
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
| 0 | char[4] | magic | `RSXR` (`52 53 58 52`) |
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

A reader may not know a section's id, or may know the id but not its version. If that section is not
critical, the reader skips `size` bytes. If it is critical, the reader ignores the **whole block**. Every
section appears at most once.

### 2.3 `STRS` - strings (version 1, critical, must be the first section)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | u8 | instrument | 0 = guitar, 1 = extended bass (5 / 6 strings, see below) |
| 1 | u8 | stringCount | all strings of the instrument: 7 for a 7-string, 8 for an 8-string (the format allows up to 16; this release plays 7 and 8) |
| 2 | u8 | columnCount | number of extended columns = stringCount - 6 for guitar |
| 3 | u8 | reserved | 0 |
| 4 | u8[columnCount] | strings | the string index of each column: guitar 6, 7, ... stringCount-1 |

Payload size = 4 + columnCount. Below, "column" means one of these extended strings, in this order.

Extended bass (instrument 1) has stringCount 5 (low B) or 6 (low B and high C), columnCount 0 and no strings.
Its MetaData tuning has six entries. The extra strings use the stock six-slot data. String index 4 = B0
(MIDI 23 + tuning[4]) and 5 = C3 (MIDI 48 + tuning[5]). Strings 0-3 stay E A D G. This applies to chords too,
where chord template columns 4 / 5 hold the low B / high C. This was checked: bass chords with string 4 and 5
scored 20/20 in the 2026-10-30 low-tuning runs.

For bass, the block only marks the arrangement, because a six-entry tuning alone cannot tell it apart from a
stock padded four-string bass. Bass blocks must omit `CHDT` and `CHNT`, even with zero records, because the
reader rejects these sections with zero columns. The manifest and .hsan `Tuning` object get `RsxrString4`
(low B) and, for six strings, `RsxrString5` (high C) after `string5`. RS Extended Range builds the bass with as
many strings as these keys say.

String indices are 0 = low E ... 5 = high e (stock), 6 = the 7th string (B1, MIDI 35) and 7 = the 8th string
(F#1, MIDI 30).

### 2.4 `CHDT` - chord templates (version 1, critical)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | templateCount: must equal the stock Chords section's count |
| 4 | records | templateCount x columnCount x { i8 fret, i8 finger } |

Record `t` belongs to stock chord template `t` (the note's `chordId`), with the columns in STRS order.

- `fret`: -1 = the string is not part of this chord, 0 = open, 1..24 = fretted.
- `finger`: -1 = none, 0 = thumb, 1..4 = index..pinky (the stock template's convention).

Payload size = 4 + 2 x templateCount x columnCount. The expected pitch is not stored. It is
`openMidi[string] + tuning[string] + fret`, using the MetaData tuning and open MIDI 35 for string 6 and 30 for
string 7. The six stock `notes[]` follow the same formula.

### 2.5 `CHNT` - chord notes (version 1, not critical)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | recordCount: must equal the stock ChordNotes section's count |
| 4 | entries | recordCount x columnCount x Tech |

A Tech entry (9 + 12 x bendCount bytes) holds the per-string fields of the stock 0x948 ChordNotes record.

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | mask: note mask bits as in the stock record's `mask[s]` (0x2000 sustain, 0x4 open, 0x800 slide, 0x1000 bend, 0x10000 vibrato, 0x400000 unpitched slide, ...) |
| 4 | i8 | slideTo: target fret, -1 = none |
| 5 | i8 | slideUnpitchTo: target fret, -1 = none |
| 6 | i16 | vibrato: as stock `vibrato[s]` (0 = none) |
| 8 | u8 | bendCount: 0..32 (the stock BendData32 `usedCount`) |
| 9 | BendValue[bendCount] | { f32 time (song seconds), f32 step (1 = half step), u32 reserved 0 }: the used entries of the stock BendData32, 12 bytes each like stock |

Record `r` belongs to stock ChordNotes record `r` (the note's `chordNotesId`). A string that is not in the
chord, or that has no technique, gets the empty entry `00 00 00 00 ff ff 00 00 00`.

### 2.6 Future sections

New data goes into new sections or a new section version. Readers skip a non-critical section they do not
know, and drop the block for a critical one. Possible candidates are per-level data and handshape/arpeggio
extensions. Extended bass needs no new section, because its extra strings use the stock columns 4 / 5.

## 3. Validation rules

A reader treats the block as absent when any of these rules fails.

1. The block starts at the first byte after MetaData; `blockSize` equals the number of body bytes
   after MetaData (nothing may follow the block). Bytes after MetaData that do not start with
   `RSXR` are not a block.
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
9. RS Extended Range also ignores a block larger than 16 MiB.

Without `CHDT`, every extended column is -1. Without `CHNT`, there are no extended techniques.

## 4. Writer rules

These rules describe the internal converter output and synthetic test fixtures.

- **When to write it:** write the block for every guitar arrangement with more than six strings (MetaData
  tuning has 7 or 8 entries), with `STRS`, `CHDT` and `CHNT` in this order, even when the counts are 0.
  Also write it for every five- or six-string bass (`STRS` instrument 1,
  [section 2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)). Bass blocks must omit
  `CHDT` and `CHNT`, even with zero records. Write no block for 6-string guitar and 4-string bass, so their
  SNG stays byte-for-byte stock.
- **Chord templates:** the six stock columns stay a valid six-column projection. Strings 0..5 have
  frets/fingers as usual, unused ones are -1, and MIDI `notes[s]` is -1 for unused strings. Never put an
  extended string's pitch into a free stock MIDI slot. A template keeps at least one stock column. For
  example, a C#5 = s6 fret 2 + s0 fret 4 becomes a one-column template with stock column s0 only. This was
  checked to load, render and score with RS Extended Range (M4). The extended columns go to `CHDT`.
  Deduplicate templates on all columns, so two chords with the same stock columns but different string-6
  frets are two templates.
- **Template names:** leave the last byte of the 32-byte name 0. The extension block supplies the extra
  columns. Names are printable ASCII, at most 30 characters.
- **Chord notes:** create a ChordNotes record when any string, stock or extended, has technique data
  **or is open** (fret 0: mask OPEN 0x4). Also set the note's `chordNotesId` and the chord note's
  CHORD_NOTES mask bit 0x1000000. This is exactly what stock tools do for open stock strings. Deduplicate
  records on stock + extended data. The data of the extended strings goes to the `CHNT` entry of that
  record. The game draws an open string's chord gem as the open-string bar only from that OPEN bit (chord
  spawner 0x7A0950). With RS Extended Range's native N-slot chords (stage 2), the 7th string runs through
  the same code. So an open extended string without a record is drawn as a fretted gem. (Changed
  2026-10-13: version 1 packages written before this rule have no record for chords whose only open string
  is extended.)
- **Double stop:** a chord with exactly two sounding strings, counting stock and extended strings together,
  gets the note's DOUBLE_STOP bit 0x02000000. RS Extended Range recounts it over all 8 strings at load anyway.
- **Single notes** on extended strings are stock note records with `string` = 6 (7). They have no block data.
- **Sections:** set `StringMask` bit 6 (bit 7 for the 8th string) in sections that use the string, for
  chords too. For bass, set bits 4 / 5 as for any stock string. A missing bit is not an error. When the song
  loads, the game ORs `1 << string` into each section's mask for every single note and chord fret, and then
  fills it in for earlier sections (checked, `SNG_Song_PostProcessNotes`, chord frets `0x55CC1C`, single
  notes `0x55CC71`, fill-in `0x55CD30`). Stock code covers strings 0-5, so bass bits 4 / 5 come for free.
  RS Extended Range adds guitar bits 6 / 7 (`Rsxr_Mid_N12`, `N13`, `TP11`). A bit that the file sets but no
  note uses is kept (not verified, but no clearing pass was seen), so write exact masks.

## 5. Rules for every extended chart (not only the block)

These are the rules for the translated loader buffer, after `XI1` / `JM1`. They are not instructions for
package export.

| What | Rule | Status |
|---|---|---|
| String index | the 7th string is index 6 (low B, MIDI 35 + tuning[6]); single notes use `string` = 6 | checked |
| SNG tuning | MetaData tuning has **7** entries for a 7-string guitar (index 6 = offset of the B), 8 for an 8-string | checked |
| Manifest key | the arrangement manifest's and the .hsan's `Tuning` object gets `RsxrString6` (the 7th string's offset) after `string5`; **never `string6`** (the stock game's song list reads every `string%d` key into room for six values, so a `string6` key crashes it) | checked |
| Chord data | stock six columns + extension block (this document) | checked |
| Package | plain PSARC (unencrypted TOC, archive flags without 0x4; SNG flags 1 = zlib, no encryption, zero IV and signature slots), loaded by RS Extended Range's `[psarc] plain` setting (or a CDLC enabler that loads plain packages); any CDLC enabler still bypasses the signature check. No Ubisoft keys anywhere | checked |
| Text | DLC keys, file names, chord/section/phrase names: printable ASCII | project rule |
| 8 strings | index 7 = F#1 (MIDI 30 + tuning[7]), 8 tuning entries, `RsxrString6` + `RsxrString7`; STRS stringCount 8, columns 6, 7. RS Extended Range plays 8 strings: notes, chords, eight-peg headstock, tuner | supported |
| Bass 5/6 | STRS instrument 1, stringCount 5 / 6, no columns ([2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)), Tuning keys `RsxrString4` / `RsxrString5`; RS Extended Range builds a 5 / 6-string bass and plays single notes and chords on strings 4 / 5 | supported |

## 6. How RS Extended Range reads it

The parser is `src/model/extblock.cpp` and the store is `src/features/nslot/hooks_extblock.cpp`. Hook XB1 at
0x55922C runs at the end of `SNG_Song_Load`, on the loader thread, before the body is freed. It parses the
bytes between the stream position and its size and validates them against the loaded song (template count
`+94h`, ChordNotes count `+0ACh`, tuning `+110h`). Then it keeps the parsed block per song. The log gets one
`extblock: SNG ...` line per loaded SNG, with the block summary or the reason the block was ignored.

Stage 2 is the native N-slot chords (`src/features/nslot/`, docs/ARCHITECTURE.md). At load, the song's chord
templates and ChordNotes records are converted once into superset records. A superset record is the stock
record followed by 8-slot arrays (template frets/fingers/MIDI, ChordNotes masks/slides/vibrato/bends). `CHDT`
fills template slots 6/7 and `CHNT` fills ChordNotes slots 6/7. The game's own chord code reads those records.
CONFIRMED (source: `src/features/nslot/hooks.cpp`, `ExtCols`): without a valid block, template slots 6/7 stay
empty. Template names carry no chord data. Native extended guitar charts always supply a block. Since 1.0 the
game's chord bend, vibrato, slide and sustain loops also run over slots 6/7, so the `CHNT` entries are used,
not only parsed.

## 7. Examples

Here are three complete blocks, with one row per field. Offsets count in decimal from the first byte of the
block (the `R` of `RSXR`). The hex under each table is the same block, for copying into a test.

### 7.1 Seven-string guitar (78 bytes)

This block has two chord templates. Template 0 is a C#5 with the 7th string at fret 2 (index finger), and
template 1 does not use the 7th string. It also has one ChordNotes record, whose 7th-string entry is sustained.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| **0** | | **Header** | |
| 0 | `52 53 58 52` | magic | `RSXR` |
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

    52 53 58 52 01 00 10 00 4e 00 00 00 03 00 00 00 53 54 52 53 01 00 01 00 05 00 00 00 00 07 01 00 06
    43 48 44 54 01 00 01 00 08 00 00 00 02 00 00 00 02 01 ff ff
    43 48 4e 54 01 00 00 00 0d 00 00 00 01 00 00 00 00 20 00 00 ff ff 00 00 00

### 7.2 Eight-string guitar (88 bytes)

This block has one chord template, an F#5 with the 8th string open and the 7th string at fret 2 (middle
finger). Its stock column s0 (fret 2) is in the stock template as usual. The open 8th string needs its `OPEN`
bit, so the chord gets a ChordNotes record ([section 4](#4-writer-rules)). With two columns, every template and
record has two entries, where column 0 = string 6 and column 1 = string 7. Each section header is shown as one
row.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| 0 | `52 53 58 52 01 00 10 00 58 00 00 00 03 00 00 00` | Header | `RSXR`, version 1, header 16, block 88, 3 sections |
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

    52 53 58 52 01 00 10 00 58 00 00 00 03 00 00 00 53 54 52 53 01 00 01 00 06 00 00 00 00 08 02 00 06 07
    43 48 44 54 01 00 01 00 08 00 00 00 01 00 00 00 02 02 00 ff
    43 48 4e 54 01 00 00 00 16 00 00 00 01 00 00 00 00 00 00 00 ff ff 00 00 00 04 00 00 00 ff ff 00 00 00

### 7.3 Five-string bass (32 bytes)

This block has only `STRS`, which marks the arrangement as an extended bass. It has no columns, because the
low B's notes and chord frets use the stock string 4
([section 2.3](#23-strs---strings-version-1-critical-must-be-the-first-section)). A six-string bass is the
same with stringCount `06`.

| Offset | Bytes | Field | Value |
|---|---|---|---|
| 0 | `52 53 58 52 01 00 10 00 20 00 00 00 01 00 00 00` | Header | `RSXR`, version 1, header 16, block 32, 1 section |
| 16 | `53 54 52 53 01 00 01 00 04 00 00 00` | `STRS` header | version 1, critical, 4 bytes |
| 28 | `01 05 00 00` | instrument, stringCount, columnCount, reserved | extended bass, 5 strings, no columns |

    52 53 58 52 01 00 10 00 20 00 00 00 01 00 00 00 53 54 52 53 01 00 01 00 04 00 00 00 01 05 00 00

## 8. Writer checklist

Use this checklist when you change the internal converter and parser.

1. Keep `STRS`, `CHDT` and `CHNT` consistent with the translated stock records and tuning counts.
2. Use `RSXR` as the four-byte magic and `RsxrString4`..`RsxrString7` as the internal manifest keys.
3. Preserve the loader buffer size. Add the non-critical `PAD0` section to tile unused bytes exactly.
4. Exercise guitar 7/8 and bass 5/6 through `tests/model/test_nativesng.cpp`,
   `tests/model/test_nativemanifest.cpp`, `tests/native/test_hooks.cpp` and the emulator tests.
5. Export player packages using the [native format](../NATIVE-FORMAT.md), never this internal representation.
