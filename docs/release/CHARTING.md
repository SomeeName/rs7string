# Charting for rs7string

**How to make and test seven- and eight-string guitar and five- and six-string bass charts for rs7string.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · **Charting** · [Changelog](CHANGELOG.md)

> [!TIP]
> **New to extended charts?** Start with [The short version](#the-short-version) for an overview. Before building,
> follow the [Format rules](#format-rules) and, for guitar, [Chords](#chords). Then
> [test the chart](#test-a-chart) before you share it.

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [The short version](#the-short-version)
- [Format rules](#format-rules)
- [Chords](#chords)
- [Build and inspect with rs7chart](#build-and-inspect-with-rs7chart)
- [Test a chart](#test-a-chart)

</details>

## The short version

- An extended chart is a normal **Lead**, **Rhythm** or **Bass** arrangement with extra string data. There is no
  separate arrangement type.
- The stock strings keep their numbers and meaning. The extra strings get the next free string indices.
- The manifest and HSAN `Tuning` keys select the built instrument's string count. The SNG tuning and an
  **RS7X block** inside the SNG must match it ([RS7X specification](RS7X-SPEC.md)).
- The package is a **plain PSARC** (unencrypted). No Ubisoft keys anywhere.
- Players need rs7string and a CDLC enabler. The game without rs7string does not support the extra strings.

| Instrument | Extra strings (index: open note) | SNG tuning entries | Extra `Tuning` keys | RS7X block |
|---|---|---|---|---|
| 7-string guitar | 6: low B (B1, MIDI 35) | 7 | `Rs7String6` | instrument 0, 7 strings, with chord data |
| 8-string guitar | 6: low B (B1, MIDI 35); 7: low F# (F#1, MIDI 30) | 8 | `Rs7String6`, `Rs7String7` | instrument 0, 8 strings, with chord data |
| 5-string bass | 4: low B (B0, MIDI 23) | 6, as stock | `Rs7String4` | instrument 1, 5 strings |
| 6-string bass | 4: low B (B0, MIDI 23); 5: high C (C3, MIDI 48) | 6, as stock | `Rs7String4`, `Rs7String5` | instrument 1, 6 strings |

Six-string guitar and four-string bass charts get none of this: no extra keys and no block.

> [!WARNING]
> **Never put `string6` (or any `string` key above `string5`) in a manifest or HSAN tuning object.** The game's
> song-list reader has room for only six `string` values there. A `string6` key writes past that space and can
> crash the game while it loads the song list. Use the `Rs7String` keys above. The note field `string=6` inside
> the SNG is fine.

## Format rules

| Item | Rule |
|---|---|
| String numbering | Keep the stock strings as they are. Guitar: 0-5 are low E, A, D, G, B, high E; the low B is **index 6** and the eighth string (low F#) **index 7**. Bass: 0-3 are E, A, D, G; the low B is **index 4** and the high C **index 5**. Never shift existing string numbers. |
| Pitch | Open pitch of a string is its open MIDI note (table above) **+ its tuning offset**; add the fret for fretted notes. Tuning offsets are semitones. |
| SNG tuning | Guitar: the MetaData tuning has **exactly one entry per string**, seven or eight. B standard is `[0, 0, 0, 0, 0, 0, 0]`; Drop A changes only entry 6 to `-2`. Bass: **six entries**, as in a stock bass chart; entry 4 is the low B offset and entry 5 the high C offset. |
| Manifest and HSAN | Every `Tuning` object keeps `string0` to `string5`. **Guitar:** add `Rs7String6` (and `Rs7String7`) with the offsets of the low B (and low F#), the same values as SNG entries 6 (and 7). **Bass:** the offsets are in `string4` (low B) and `string5` (high C), as in the SNG; add `Rs7String4` (and `Rs7String5`) with the same values. On bass these keys only say how many strings there are: one more string per key. |
| RS7X block | Append it after MetaData inside the SNG body. Guitar: `STRS`, `CHDT` and `CHNT`, even with no chords. Bass: `STRS` only, which marks the arrangement as five- or six-string. See [Chords](#chords) and the [specification](RS7X-SPEC.md). |
| Single notes | Ordinary note records with the extra string's index (`string=6` or `7` on guitar, `string=4` or `5` on bass). No extra data is needed. |
| Section masks | Set the extra strings' bits in `StringMask` for sections that use them, chords included: guitar bit 6 (and bit 7 on eight strings), bass bits 4 and 5. The game also adds missing bits itself when the song loads. |
| Package | **Plain PSARC:** unencrypted TOC, archive flags without `0x4`; SNG flags `1` (zlib only), zero IV and signature slots, no encryption. No Ubisoft keys. |
| Text | Printable **ASCII** for DLC keys, file names, and chord, section and phrase names. Spell the `Rs7String` keys exactly like this. |
| Identifiers | Give the package and arrangement their own identifiers where needed. Do not assume separate score storage for extended arrangements. |

Low tunings need no workaround: no "low tuning fix" mod and no A220 reference pitch. Notes are detected down to the
lowest open string of the chart's tuning, as low as G0 (a five-string bass in drop G); lower open strings are not
detected.

### The `Tuning` object, key by key

Guitar and bass use the `Rs7String` keys differently, because a stock `Tuning` object always has six `string`
keys:

- **Guitar** uses all six for its stock strings, and a seventh `string` key would crash the game. So the extra
  strings' offsets are in the new keys: `Rs7String6` is the low B, `Rs7String7` the low F#.
- **Bass** uses only `string0` to `string3`; `string4` and `string5` are there but unused. The low B's offset goes
  in `string4` and the high C's in `string5`. `Rs7String4` and `Rs7String5` only tell rs7string that the bass has
  five or six strings. Give them the same values as `string4` and `string5`.

The number in an `Rs7String` key is always the string's index, the same number the notes use.

```jsonc
// 7-string guitar, Drop A (low B tuned down two semitones)
{
  "string0": 0, "string1": 0, "string2": 0, "string3": 0, "string4": 0, "string5": 0, // E A D G B e, as stock
  "Rs7String6": -2 // low B: its offset is here
}

// 8-string guitar, F# standard
{
  "string0": 0, "string1": 0, "string2": 0, "string3": 0, "string4": 0, "string5": 0, // E A D G B e, as stock
  "Rs7String6": 0, // low B
  "Rs7String7": 0  // low F#
}

// 6-string bass, low B tuned down to A, high C in standard
{
  "string0": 0, "string1": 0, "string2": 0, "string3": 0, // E A D G, as stock
  "string4": -2,    // low B: its offset is here
  "string5": 0,     // high C: its offset is here
  "Rs7String4": -2, // "this bass has a low B": same value as string4
  "Rs7String5": 0   // "and a high C": same value as string5
}
```

A five-string bass leaves out `Rs7String5` and keeps `string5` at `0`.

## Chords

On a five- or six-string bass, chords use the stock chord template as they are: the low B is column 4 and
the high C column 5, like any other string, so a chord with the low B is one ordinary chord. Do not split those
notes off as single notes: the game would draw them outside the chord frame, as tilted single notes with a stem,
and score them separately. The bass block has no chord data; the extra `CHDT` / `CHNT` columns are for guitar only.

The byte layout, validation and full writer rules are in the [RS7X specification](RS7X-SPEC.md).
For new guitar charts:

- **Where:** write the RS7X block after MetaData inside the decompressed SNG body, then compress the
  whole body. Include `STRS`, `CHDT` and `CHNT` in that order, even with zero records. Write no block
  for ordinary six-string guitar or four-string bass charts.
- **Stock columns:** keep the six stock template columns as a valid view of strings 0-5, with at
  least one sounding stock column. Unused fret, finger and MIDI columns are `-1`. The seventh and eighth
  columns go into `CHDT`, never into a spare stock MIDI column.
- **Deduplicate** templates and ChordNotes on all columns, including strings 6 and 7.
- **Names:** keep the template's `name[31]` byte zero. Chord names are printable ASCII, at most 30
  characters. (Older packages used that byte for string-6 data; new writers use RS7X.)
- **ChordNotes:** write a record whenever any sounding string is open or has technique data. Set
  `chordNotesId` and the chord's `CHORD_NOTES` bit (`0x1000000`). An open string 6 needs its `OPEN`
  bit (`0x4`) in `CHNT`, or it is drawn as a fretted gem instead of the open-string bar. The same goes
  for an open string 7.
- **Double stops:** count all sounding strings; exactly two gets `DOUBLE_STOP` (`0x02000000`).
- **Section masks:** include strings 6 and 7.

How rs7string shows and judges chords:

- With `[chords] diagrams7=1` (the default) the mod shows chord diagrams with a row for the seventh and eighth string. Some generic chords
  from the game's own chord database can still show six rows.
- The mod uses the `CHNT` technique data for chords.
- The default `wide_misses=2` can accept seven- and eight-note chords with up to two missing notes. Use
  `wide_misses=0` when you check that every intended note is required.

> [!NOTE]
> The final look of the diagrams, technique judging and real-guitar chord play still need a final
> test.

## Build and inspect with rs7chart

rs7chart, a command-line tool that builds and checks plain packages, will be published later. Until then, use
[DotCharter](https://github.com/SomeeName/DotCharter) to make charts.

## Test a chart

1. Inspect and verify the package outside the game folder. Keep a six-string control version where
   useful.
2. Close Rocksmith. Back up any package with the same name, then copy the extended package into
   `dlc`. Use the supported game build, a CDLC enabler and rs7string with `[psarc] plain=1`.
3. Check that the song list loads and shows the extended tuning name. Check the extra strings in the
   tuner and its precision view. The `Rs7String` keys select the built instrument; the SNG alone cannot turn on
   extra strings. Its tuning must match the built count. A guitar count mismatch keeps the detector at stock
   width and logs `song detector: not widened: the guitar was built with another string count (song, guitar)`.
4. Start the song. Use F9 **Advanced > Status** and `rs7string.log` to confirm the string count. Check audio and
   every lane. In standard tuning the open extra strings should target MIDI 35 (low B) and 30 (low F#)
   on guitar, 23 (low B) and 48 (high C) on bass; each fret adds one.
5. Play notes on the extra strings with a real instrument, then the same notes on stock strings as a control. Check
   hit/miss feedback, sustains, slides, bends, vibrato and harmonics separately. Do not judge from
   visuals alone.
6. Guitar: test open and fretted chords on the extra strings, double stops, full chords, finger markers, section
   highlights and techniques. Use `wide_misses=0` for a strict full-chord check, then test the default
   as well.
7. Seek, restart, finish the song, and load a six-string guitar or four-string bass control. Check for
   leftover extra-string visuals and crashes.

> [!CAUTION]
> Do not load an extended chart without rs7string. Remove it before you test an unmodded game.

> [!TIP]
> With a bug report, include the chart version, mod version, tuning, settings, what you saw and the
> relevant log lines.
