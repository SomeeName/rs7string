# Charting for RS Extended Range

**How to make and test seven- and eight-string guitar and five- and six-string bass charts for RS Extended Range.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · **Charting** · [Changelog](CHANGELOG.md)

> [!TIP]
> Start with [The short version](#the-short-version), then [test your chart](#test-a-chart) before sharing it.
> Tool developers can use the [chart tools guide](CHART-TOOLS.md) and [native format specification](NATIVE-FORMAT.md).

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [The short version](#the-short-version)
- [Format rules](#format-rules)
- [Chords](#chords)
- [Charts made for earlier versions](#charts-made-for-earlier-versions)
- [Test a chart](#test-a-chart)

</details>

## The short version

Use [DotCharter](https://github.com/SomeeName/DotCharter) to make an extended chart:

1. Create or import a song and choose a normal **Lead**, **Rhythm** or **Bass** arrangement.
2. Set the string count and tuning. Use seven or eight strings for guitar, or five or six for bass.
3. Chart notes, chords and techniques on all the strings as usual.
4. Choose DotCharter's **RS Extended Range** export. It writes the native format the mod reads,
   in a file named `<name>_rsxr_p.psarc`.
5. Follow [Test a chart](#test-a-chart) before sharing the package.

Players need RS Extended Range and a CDLC enabler for the supported game build. A game without the mod
does not list native packages. Six-string guitar and four-string bass arrangements keep their stock format,
even when they share a package with extended arrangements.

## Format rules

Every string is numbered by its position, **thickest first**, starting at 0. The numbering stays the same when
you change the tuning. It does not follow the resulting pitches.

| Instrument | String indices in standard tuning, thickest first |
|---|---|
| 7-string guitar | 0 B, 1 E, 2 A, 3 D, 4 G, 5 B, 6 high E |
| 8-string guitar | 0 F#, 1 B, 2 E, 3 A, 4 D, 5 G, 6 B, 7 high E |
| 5-string bass | 0 B, 1 E, 2 A, 3 D, 4 G |
| 6-string bass | 0 B, 1 E, 2 A, 3 D, 4 G, 5 high C |

DotCharter's export handles the package details for you. If you use another tool, it must write the
[native format](NATIVE-FORMAT.md). The instrument, string count and tuning must match in every file:

| Item | Rule |
|---|---|
| Arrangement XML | Extended arrangements have `<tuning extendedRange="1" strings="N" ...>`. Tuning, note and chord indices all use the order above. |
| Tuning | One whole-number semitone offset per string, from -24 to 24, measured from that string's standard open note. |
| Manifest and HSAN | Each extended arrangement has `"ExtendedRange": {"Version": 1}` and a `Tuning` object with exactly `string0` through `string{N-1}`. |
| Chart | An extended chart has an `SNXR` header and one tuning entry and chord column per string. |
| Package | Plain (unencrypted) PSARC with `PSXR` magic, named `<name>_rsxr_p.psarc` when any arrangement is extended. |

### Charter XML

[Charter](https://github.com/Lordszynencja/Charter) 0.23.3 and later saves seven- and eight-string guitar XML in
the string order and with the standard notes above, with `strings="7"` or `strings="8"` on `<tuning>`. It does not
write the marker. Add `extendedRange="1"` to that `<tuning>` element to make the file follow this format.

Charter's five- and six-string bass is a different instrument. Its extra string sits above the G instead of
below the E, so the strings run E A D G B. Those files do not match this format, so chart five- and six-string bass in DotCharter.

### The `Tuning` object, key by key

In seven-string guitar Drop A, only the low B changes. String 0 has offset `-2`, and strings 1 through 6 stay at `0`.
The arrangement's manifest and HSAN entry both contain:

```json
"ExtendedRange": { "Version": 1 },
"Tuning": {
  "string0": -2, "string1": 0, "string2": 0, "string3": 0,
  "string4": 0, "string5": 0, "string6": 0
}
```

Eight-string F# standard has eight zero offsets. Five-string bass B standard has five, and six-string bass B standard
has six. The XML keeps at least six tuning attributes for compatibility. The manifest and chart have exactly the
chosen string count. See the [XML rules](NATIVE-FORMAT.md#arrangement-xml).

Low tunings do not need a separate low tuning fix or an A220 reference pitch. The lowest supported open note is G0.

## Chords

Chart a chord across all its strings as one chord, on guitar or bass. Keep each string's fret, finger and
techniques in its own column. Do not split the extra strings off as separate notes.

Test open and fretted chords, double stops, finger markers and techniques on the extra strings.
Chord diagrams show the extra rows with `[chords] diagrams7=1`, which is the default. Some generic chords from the
game's chord database still show six rows. With the default `wide_misses=2`, the game can accept seven- and
eight-note chords with up to two missing notes. Test with `wide_misses=0` first for a strict full-chord check, then
try the default too. See [settings](SETTINGS.md).

## Charts made for earlier versions

The mod no longer reads charts in the old format. To convert them, drag one or more old `_p.psarc` files, or a
whole folder such as `dlc`, onto `tools/rsxr-convert.exe` from the release zip.

For every old-format chart, the converter:

- writes `<name>_rsxr_p.psarc` next to the old file
- moves the old file to a subfolder `old-format` as `<name>_p.disabled.psarc`, so the game no longer loads it

The converter leaves normal 6-string and 4-string customs, official DLC and charts already in the new format alone.

To go back to an older version of the mod, rename an old file to `<name>_p.psarc`, move it out of
`old-format` and delete the `<name>_rsxr_p.psarc` beside it.

## Test a chart

1. Export the package and inspect it outside the game folder. Check the instrument, string count and tuning.
2. Close Rocksmith. Back up any package with the same name, then copy `<name>_rsxr_p.psarc` into `dlc`.
   Use the supported game build, a CDLC enabler and RS Extended Range with `[psarc] plain=1`.
3. Check that the song list shows the extended tuning name. Tune every string, including in the tuner's
   precision view. The manifest and chart must agree on the instrument, string count and tuning.
4. Start the song. F9 **Advanced > Status** and `rsxr.log` should report the right string count.
   Check the audio, lanes and headstock.
5. Play notes on every string with a real instrument. Check hit/miss feedback, sustains, slides, bends,
   vibrato and harmonics separately. Check open notes as well as fretted notes, because each fret raises the
   pitch by one semitone. Do not judge from the visuals alone.
6. Check the [chords](#chords), section highlights and generated difficulty levels.
7. Seek, restart and finish the song. Then load a six-string guitar or four-string bass song as a control,
   and check for leftover extra-string visuals and crashes.

> [!TIP]
> When you report a bug, include the chart version, mod version, instrument, tuning, settings, what you saw and
> the relevant log lines. Check logs for personal information before sharing them.
