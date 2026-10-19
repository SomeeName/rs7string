# RS7X: the SNG extension block for extended-range charts

**Byte-level specification of the block that carries chord data for strings beyond the stock six.**

[Contributing](../../CONTRIBUTING.md) · [Development](../DEVELOPMENT.md) · [Architecture](../ARCHITECTURE.md) · **RS7X spec** · [Releasing](RELEASING.md) · [Agents](../../AGENTS.md)

| | |
|---|---|
| Format version | 1 (2026-10-13) |
| Document revision | 2026-10-14, for the rs7string 1.0 release candidate: byte layout and validation rules re-checked against the reader, unchanged |
| Tracking issue | SomeeName/rs7string#18 |
| Audience | DotCharter (and any other chart writer) exporting 7-string (later 8-string, 5/6-string bass) arrangements for Rocksmith 2014 Remastered with the rs7string mod |
| Reference writer | The maintainer's rs7chart tool (to be published later: `src/rs7chart/sng.py` `ExtBlock`, writer in `convert.py`, tests in `tests/test_extblock.py`) |
| Reference reader | rs7string `src/model/extblock.cpp`, `rs7ext::Parse` |

> [!TIP]
> Chart-tool authors: [section 9](#9-dotcharter-checklist-state-of-dotcharter-master-45ff88cc8-2026-10-14)
> is the DotCharter checklist, and [section 4](#4-writer-rules-dotcharter) has the writer rules.
> Chart authors who only need the basics should read the [charting guide](CHARTING.md).

## Contents

1. [Why a block, and where it goes](#1-why-a-block-and-where-it-goes)
2. [Byte layout](#2-byte-layout): [header](#21-header-16-bytes), [section](#22-section-12-byte-header--payload),
   [`STRS`](#23-strs---strings-version-1-critical-must-be-the-first-section),
   [`CHDT`](#24-chdt---chord-templates-version-1-critical),
   [`CHNT`](#25-chnt---chord-notes-version-1-not-critical), [future sections](#26-future-sections)
3. [Validation rules](#3-validation-rules)
4. [Writer rules (DotCharter)](#4-writer-rules-dotcharter)
5. [Stable rules for 7-string charts](#5-stable-rules-for-7-string-charts-all-of-them-not-only-the-block)
6. [How rs7string reads it](#6-how-rs7string-reads-it)
7. [Example](#7-example)
8. [Results](#8-results-2026-10-13-rs7string-featneck--rs7x-september-2022-exe)
9. [DotCharter checklist](#9-dotcharter-checklist-state-of-dotcharter-master-45ff88cc8-2026-10-14)

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

- CONFIRMED (static): the loader reads the body size from the container, inflates with zlib's
  `uncompress` (0xE316E0: `inflateInit_`, `inflate(Z_FINISH)`, `*destLen = total_out`) into a
  buffer of exactly that size and fails only if `uncompress` fails or the inflated length differs
  from the size field (0x558DBD / 0x558DC9). It then reads the sections from a memory stream
  (vtable 0x11E7C64; `+10h` buffer, `+14h` position, `+18h` size; reads clamp at the end) and, after
  the last MetaData read (0x55922A), destroys the stream and returns success (0x55922C..0x559246).
  The position is never compared with the size, so bytes after MetaData are never read.
- CONFIRMED (in game, section 8): packages with the block load and play; rs7string's XB1 hook logs
  the stream position (end of MetaData) below the body size on every such load.
- Consequence: the block must be **inside** the zlib stream. Data after the zlib stream (before the
  56-byte signature slot) is a different matter and is not used. A body larger than the size field
  makes `uncompress` fail (Z_BUF_ERROR) and the song does not load.

Other readers: HYPOTHESIS (not tested): Rocksmith2014.NET-style readers parse section by section
and stop after MetaData, so they ignore the block. `tools/rs7chart` reads and validates it
(`rs7chart dump` prints it and flags rule violations).

## 2. Byte layout

All integers little-endian, no alignment or padding anywhere. `i8`/`u8` are bytes, `i16`/`u16`
2 bytes, `u32` 4 bytes, `f32` IEEE single.

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
| 0 | u8 | instrument | 0 = guitar (1 = bass: reserved for a later version, see 6) |
| 1 | u8 | stringCount | all strings of the instrument: 7 for a 7-string, 8 for an 8-string (max 16) |
| 2 | u8 | columnCount | number of extended columns = stringCount - 6 for guitar |
| 3 | u8 | reserved | 0 |
| 4 | u8[columnCount] | strings | the string index of each column: guitar 6, 7, ... stringCount-1 |

Payload size = 4 + columnCount. "Column" below means one of these extended strings, in this order.
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
extensions, bass column mapping.

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
5. `STRS`: instrument 0; 7 <= stringCount <= 16; **stringCount == the number of MetaData tuning
   entries**; columnCount == stringCount - 6; strings == 6, 7, ..., stringCount - 1.
6. `CHDT`: templateCount == stock chord template count; payload exactly 4 + 2 x count x columns;
   fret in {-1, 0..24}; finger in {-1, 0..4}; fret -1 implies finger -1.
7. `CHNT`: recordCount == stock ChordNotes count; bendCount <= 32; slideTo and slideUnpitchTo in
   {-1, 0..24}; the entries consume the payload exactly.
8. Each section's payload is consumed exactly (no trailing bytes inside a section).

Absent `CHDT` = every extended column is -1. Absent `CHNT` = no extended techniques.

## 4. Writer rules (DotCharter)

- **When:** write the block for every arrangement with more than six strings (MetaData tuning has
  7 or 8 entries), with `STRS`, `CHDT` and `CHNT` in this order, even when the counts are 0. Write
  no block for 6-string guitar and 4-string bass: their SNG stays byte-for-byte stock.
- **Chord templates:** the six stock columns stay a valid six-column projection (strings 0..5:
  frets/fingers as usual, unused = -1, MIDI `notes[s]` = -1 for unused). Never put an extended
  string's pitch into a free stock MIDI slot. A template keeps at least one stock column (one-column
  templates such as C#5 = s6 fret 2 + s0 fret 4 -> stock column s0 only: CONFIRMED to load, render
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
  gets the note's DOUBLE_STOP bit 0x02000000 (rs7string recounts it over 7 strings at load anyway).
- **Single notes** on extended strings are stock note records with `string` = 6 (7); no block data.
- **Sections:** `StringMask` bit 6 (bit 7 for the 8th string) set in sections that use the string,
  for chords too.

## 5. Stable rules for 7-string charts (all of them, not only the block)

| What | Rule | Status |
|---|---|---|
| String index | the 7th string is index 6 (low B, MIDI 35 + tuning[6]); single notes use `string` = 6 | CONFIRMED (M1/M2) |
| SNG tuning | MetaData tuning has exactly **7** entries (index 6 = offset of the B); rs7string switches 7-string mode on for exactly 7 entries | CONFIRMED (mode.cpp, S4) |
| Manifest key | the arrangement manifest's and the .hsan's `Tuning` object gets `Rs7String6` (the 7th string's offset) after `string5`; **never `string6`** (the song-list reader 0x56D0B0 writes every `string%d` into a 6-int stack array: `string6` overwrites the /GS cookie and crashes the stock game) | CONFIRMED |
| Chord data | stock six columns + RS7X block (this document); old packages: name[31] | CONFIRMED (stage 1) |
| Package | plain PSARC (unencrypted TOC, archive flags without 0x4; SNG flags 1 = zlib, no encryption, zero IV and signature slots), loaded by rs7string's `[psarc] plain` patches (or a CDLC enabler with the plain-psarc PR #1); any CDLC enabler still bypasses the signature check. No Ubisoft keys anywhere | CONFIRMED |
| Text | DLC keys, file names, chord/section/phrase names: printable ASCII | project rule |
| 8 strings | index 7 = F#1 (MIDI 30 + tuning[7]), 8 tuning entries, `Rs7String6` + `Rs7String7`; STRS stringCount 8, columns 6, 7. rs7string 1.0 does not play 8-string charts (prepared behind `[neck] eight=0`, off; several game paths still assume at most 7): writers may implement it, but do not ship such packages to players yet | format defined, game support after 1.0 |
| Bass 5/6 | not defined in version 1 (instrument 1 is reserved; a reader ignores such a block) | later |

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
Stage 1 rewrote name[31] from the block instead; that is gone. Since `[chords] native_chordloops`
(on by default in 1.0) the game's chord bend, vibrato, slide and sustain loops also run over slots
6/7, so the `CHNT` entries are used, not only parsed.

## 7. Example

7 strings, two chord templates (0: C#5 with string 6 at fret 2, finger 1; 1: no string-6 column),
one ChordNotes record whose string-6 entry is sustained (78 bytes):

    52 53 37 58  01 00  10 00  4e 00 00 00  03 00 00 00     RS7X, v1, header 16, block 78, 3 sections
    53 54 52 53  01 00  01 00  05 00 00 00                  STRS v1 critical, 5 bytes
      00 07 01 00 06                                        guitar, 7 strings, 1 column, -, string 6
    43 48 44 54  01 00  01 00  08 00 00 00                  CHDT v1 critical, 8 bytes
      02 00 00 00                                           2 templates
      02 01                                                 template 0: fret 2, finger 1
      ff ff                                                 template 1: -1, -1
    43 48 4e 54  01 00  00 00  0d 00 00 00                  CHNT v1, 13 bytes
      01 00 00 00                                           1 record
      00 20 00 00  ff  ff  00 00  00                        mask 0x2000 (sustain), no slide, no vibrato, 0 bends

8 strings, one template (F#5: string 7 open, string 6 fret 2 finger 2, stock column s0 fret 2), no
ChordNotes records (70 bytes):

    52 53 37 58 01 00 10 00 46 00 00 00 03 00 00 00
    53 54 52 53 01 00 01 00 06 00 00 00   00 08 02 00 06 07
    43 48 44 54 01 00 01 00 08 00 00 00   01 00 00 00   02 02   00 ff
    43 48 4e 54 01 00 00 00 04 00 00 00   00 00 00 00

## 8. Results (2026-10-13, rs7string feat/neck + RS7X, September 2022 exe)

<details>
<summary><b>Test packages and in-game results</b></summary>

Packages regenerated by `tools/rs7chart` (`tests/rs7/generated`). Static: the 6-string controls are
byte-identical to the previous build; every 7-string package differs only in its SNG, whose stock
sections are identical to the previous (name[31]) build apart from name[31] = 0, plus the block; the
`rs7c_m4_1_pairs_b31` twin's SNG is byte-identical to the old M4-1 SNG.

In game, virtual guitarist (`[test] vguitar=1`, lead 40 ms, chart mode, `vguitar_chord6=play`),
compared with the merge regression (rs7string README):

| Package | Chord data | `extblock` log at load | Hits | Merge regression |
|---|---|---|---|---|
| RS7C M4-1 pairs | block only | body 6693, stock sections end at 6620, block 73 bytes, 4/4 templates with string 6 | 25/25 | 25/25 |
| RS7C M4-2 three-note chords | block only | body 10278, end 10192, block 86 bytes, 6/6 templates, 1 ChordNotes record | 15/21 | 15/21 |
| RS7C M4-3 full 7-string chords | block only | body 22969, end 22833, block 136 bytes, 4/4 templates, 7 records | 12/16 | 12/16 |
| RS7C M4-1 pairs (byte 31) | name[31] only (old format) | body 6620, end 6620, no block -> name[31] fallback, 4/4 templates | 25/25 | (= M4-1) 25/25 |
| RS7C M4-1 pairs (6-str ctrl) | - | no block, 6 tuning entries | 25/25 | 25/25 |
| RS7C M4-2 three-note (6-str ctrl) | - | no block | 21/21 | 21/21 |
| RS7C M4-3 full (6-str ctrl) | - | no block | 16/16 | 16/16 |

CONFIRMED: the stock loader stops at the end of MetaData (logged position < body size) and the songs
with a block load and play; the block is read and applied (string-6 columns of block-only packages
are judged; Q1 log `string 6 fret 2 -> MIDI 37 added`); packages without a block fall back to name[31].
In game the SNG is loaded after the pre-song tuner is skipped (log 75-80 s), not at song selection.

Update 2026-10-14: the extended strings' CHNT data is now read by the native chord paths
(`native_chordloops`, section 6). Release-candidate runs: chords chart 16/16, RS7 Test 36/36 (virtual
guitarist). Whether a sustained chord now draws its string-6 tail was not checked separately
(HYPOTHESIS until checked).

</details>

## 9. DotCharter checklist (state of DotCharter master 45ff88cc8, 2026-10-14)

DotCharter's editor already allows more than six strings, but its Rocksmith export is six-string only
(`DotCharter.Services/Export/Sng/SngFromDomainMapper.cs`: `SngStringLimits.MaxStringIndex = 5`, and
`ChartExportValidator.KeyStringOutOfRange` is an error that blocks the export). To export a 7-string
arrangement:

1. **Validator:** allow string index 6 for 7-string guitar arrangements (keep the error for 8+ until
   rs7string plays 8 strings, and for bass).
2. **MetaData tuning:** write 7 entries (index 6 = the B offset), not a fixed `short[6]`.
3. **Notes:** single notes on the 7th string = stock note records with `string` = 6; section
   `StringMask` bit 6 where the string is used (chords too).
4. **Chord templates:** stock six columns as a valid projection (section 4); the 7th string's
   fret/finger go to `CHDT`; deduplicate on all seven columns; `name[31]` = 0; ASCII names <= 30
   chars; stock MIDI `notes[]` only for strings 0..5 (`MapToMidiNotes` stays six-wide, never put the
   B into a free slot).
5. **ChordNotes:** create a record when any string (stock or 7th) has a technique or is open; the 7th
   string's mask/slide/vibrato/bends go to `CHNT`; empty entry `00 00 00 00 ff ff 00 00 00`.
6. **Double stop:** count the 7th string when setting 0x02000000.
7. **Block:** append RS7X (`STRS`, `CHDT`, `CHNT`, in that order, even with zero records) after MetaData
   **inside** the zlib body; the container's body size includes it. No block for 6-string / bass.
8. **Manifest + HSAN:** `Tuning` gets `Rs7String6` after `string5`; never `string6`.
9. **Package:** plain PSARC only (no Ubisoft keys), ASCII DLC keys and file names.
10. **Import (round trip):** DotCharter's SNG reader should parse the block (section 3 rules) so a
    7-string package opens with its 7th string; on a rule failure fall back to name[31] (old packages)
    and else to six strings.
11. **Test:** compare a DotCharter export of one of the rs7chart test charts with rs7chart's own
    output (`rs7chart dump` validates the block and flags rule violations), then play it in game:
    the `extblock: SNG ...` log line must say the block was read (not "ignored").
