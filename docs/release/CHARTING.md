# Charting for rs7string

**How to make and test seven-string charts for rs7string 1.0.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · **Charting** · [Changelog](CHANGELOG.md)

## Contents

- [The short version](#the-short-version)
- [Format rules](#format-rules)
- [Chords](#chords)
- [Build and inspect with rs7chart](#build-and-inspect-with-rs7chart)
- [Test a chart](#test-a-chart)

## The short version

- A seven-string chart is a normal guitar **Lead** or **Rhythm** arrangement with extra string data.
  There is no separate seven-string arrangement type.
- The low B is **string index 6**. Strings 0-5 keep their stock meaning.
- The SNG tuning has **seven entries**. This is what turns on seven-string mode.
- The manifest and HSAN tuning get the extra key **`Rs7String6`**. Never `string6`.
- Chords on the seventh string go into an **RS7X block** ([specification](RS7X-SPEC.md)).
- The package is a **plain PSARC** (unencrypted). No Ubisoft keys anywhere.
- Players need rs7string and a CDLC enabler. The game without rs7string does not support the seventh
  string.

> [!WARNING]
> **Never put `string6` in a manifest or HSAN tuning object.** The game's song-list reader has room
> for only six `string` values there. A `string6` key writes past that space and can crash the game
> while it loads the song list. The note field `string=6` inside the SNG is fine.

## Format rules

| Item | Rule |
|---|---|
| String numbering | Keep stock strings 0-5: low E, A, D, G, B, high E. Add the low B as **index 6**. Do not shift existing string numbers. |
| Pitch | Open pitch of string 6 is MIDI **35 + tuning[6]**; add the fret for fretted notes. Tuning offsets are semitones. |
| SNG tuning | The MetaData tuning has **exactly seven entries**. B standard is `[0, 0, 0, 0, 0, 0, 0]`; Drop A changes only the last entry to `-2`. |
| Manifest and HSAN | In each relevant `Tuning` object, keep `string0` to `string5`, then add **`Rs7String6`** with the seventh offset. It must agree with the SNG. |
| Single notes | Ordinary note records with `string=6`. No extra data is needed. |
| Section masks | Set bit 6 in `StringMask` for sections that use the seventh string, chords included. |
| Package | **Plain PSARC:** unencrypted TOC, archive flags without `0x4`; SNG flags `1` (zlib only), zero IV and signature slots, no encryption. No Ubisoft keys. |
| Text | Printable **ASCII** for DLC keys, file names, and chord, section and phrase names. Spell `Rs7String6` exactly like this. |
| Identifiers | Give the package and arrangement their own identifiers where needed. Do not assume separate score storage for seven-string arrangements. |

For B standard, the manifest and HSAN tuning object is:

```json
{
  "string0": 0,
  "string1": 0,
  "string2": 0,
  "string3": 0,
  "string4": 0,
  "string5": 0,
  "Rs7String6": 0
}
```

> [!NOTE]
> Eight-string and extended bass charts are not part of this release. Do not give eight-string test
> packages to players.

## Chords

The byte layout, validation and full writer rules are in the [RS7X specification](RS7X-SPEC.md).
For new charts:

- **Where:** write the RS7X block after MetaData inside the decompressed SNG body, then compress the
  whole body. Include `STRS`, `CHDT` and `CHNT` in that order, even with zero records. Write no block
  for ordinary six-string guitar or four-string bass charts.
- **Stock columns:** keep the six stock template columns as a valid view of strings 0-5, with at
  least one sounding stock column. Unused fret, finger and MIDI columns are `-1`. The seventh column
  goes into `CHDT`, never into a spare stock MIDI column.
- **Deduplicate** templates and ChordNotes on all columns, including string 6.
- **Names:** keep the template's `name[31]` byte zero. Chord names are printable ASCII, at most 30
  characters. (Older packages used that byte for string-6 data; new writers use RS7X.)
- **ChordNotes:** write a record whenever any sounding string is open or has technique data. Set
  `chordNotesId` and the chord's `CHORD_NOTES` bit (`0x1000000`). An open string 6 needs its `OPEN`
  bit (`0x4`) in `CHNT`, or it is drawn as a fretted gem instead of the open-string bar.
- **Double stops:** count all sounding strings; exactly two gets `DOUBLE_STOP` (`0x02000000`).
- **Section masks:** include string 6.

How rs7string shows and judges chords:

- With `[chords] diagrams7=1` (the default) the mod shows seven-row chord diagrams. Some generic chords
  from the game's own chord database can still show six rows.
- The mod uses the `CHNT` technique data for chords.
- The default `wide_misses=2` can accept seven-note chords that miss their low B. Use
  `wide_misses=0` when you check that every intended note is required.

> [!NOTE]
> The final look of the diagrams, technique judging and real-guitar chord play still need a final
> test.

## Build and inspect with rs7chart

rs7chart reads, writes and verifies plain packages. **It will be published later**; the download
link will be added then. It refuses encrypted input and output paths containing `steamapps`. Build
into a separate working folder, then copy a checked package yourself while the game is closed.

<details>
<summary><b>rs7chart commands</b></summary>

From the tool folder:

```powershell
uv sync
uv run rs7chart build my_chart.yaml -o <output folder>
uv run rs7chart verify <output folder>\my_chart_p.psarc
uv run rs7chart dump <output folder>\my_chart_p.psarc --notes 40
```

- Replace `my_chart` and `<output folder>` with your chart's output name and folder.
- Check the dump for seven tuning entries, matching manifest and HSAN offsets, string-6 notes,
  extension records and section masks.
- `verify` checks round-trip consistency. It does not check musical correctness or whether the game
  accepts the chart.
- The generated notes sheet and sidecar help inspection. A sidecar does not replace the chord data
  embedded in the SNG.

A minimal YAML example and the final rs7chart setup instructions are still to come. Check which
chord, difficulty-level and tone features the tool supports before you rely on them.

</details>

## Test a chart

1. Inspect and verify the package outside the game folder. Keep a six-string control version where
   useful.
2. Close Rocksmith. Back up any package with the same name, then copy the seven-string package into
   `dlc`. Use the supported game build, a CDLC enabler and rs7string with `[psarc] plain=1`.
3. Check that the song list loads and shows the seven-string tuning name. Check low-B tuning and
   navigation in the precision view. A missing `Rs7String6` can break the pre-song steps even if the
   SNG later turns on seven-string mode.
4. Start the song. Use F9 **Status** and `rs7string.log` to confirm seven-string mode. Check audio and
   all seven lanes. In B standard, open string 6 and frets 3/5/7 should target MIDI 35/38/40/42.
5. Play string-6 notes with a real guitar, then the same notes on stock strings as a control. Check
   hit/miss feedback, sustains, slides, bends, vibrato and harmonics separately. Do not judge from
   visuals alone.
6. Test open and fretted seventh-string chords, double stops, full chords, finger markers, section
   highlights and techniques. Use `wide_misses=0` for a strict full-chord check, then test the default
   as well.
7. Seek, restart, finish the song, and load a six-string control. Check for leftover seventh-string
   visuals and crashes.

> [!CAUTION]
> Do not load a seven-string chart without rs7string. Remove it before you test an unmodded game.

> [!TIP]
> Developers can test with the virtual guitarist ([SETTINGS](SETTINGS.md#virtual-guitarist-test)).
> Keep those results separate from real-guitar tests and set `vguitar=0` again afterwards. With a bug
> report, include the chart version, mod version, tuning, settings, what you saw and the relevant log
> lines.
