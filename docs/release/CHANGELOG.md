# Changelog

**What is new in each rs7string version.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · **Changelog**

## 1.0.0-beta.2

**Beta update focused on RSMods colour compatibility and tuner fixes.**

### Fixes

- Fix incorrect colours with RSMods custom colours, Extended Range mode and colourblind mode,
  including headstock pegs and fret numbers.
- Keep the selected tuning peg and its coloured glow synchronized when RSMods changes the palette.
- Fix the seven-string tuner switching back to E after initially selecting low B.

### Improvements

- Automatically use RSMods' configured Extended Range low B colour for the seventh string.
- Detect RSMods reliably regardless of DLL loading order.
- Add an F9 setting to disable automatic colour matching and use your own saved colours.

### Updating

Close Rocksmith and back up your existing installation. Extract the ZIP and copy everything inside
`bin/Release/` into the game folder. Keep your existing `rs7string.ini` to preserve your settings;
new settings use their defaults.

RSMods still controls Extended Range activation timing. A brief colour transition when moving from
the tuner into a song may remain.

## 1.0

The first release: seven-string guitar charts in Rocksmith 2014 Remastered on Windows.

### Playing

- Play on a seven-string highway, with note detection and scoring on the low B string. Alternate
  tunings work.
- Play chords that use the seventh string, with seven-row chord diagrams, finger numbers,
  open-string bars and technique visuals.
- See a fret grid and fret bars that reach across all seven strings.
- Play with the game's reversed string order. The highway, fret bars and floor bars follow it.

### Guitar and tuner

- Select a seven-string song and the guitar is built with seven strings. Six-string songs rebuild it
  with six. Session Mode, Guitarcade, Lessons and the technique hub also return it to six strings.
- Tune all seven strings before a song, starting on low B. Seven-string tuning names such as
  `B Standard 7Str` appear in the song list, on song pages and in the tuner.
- Get a seven-peg headstock, built automatically from your own installed game. No separate
  model-building tools are needed. Do not share the generated model files.
- Use right- or left-handed profiles. The in-line seven-string headstock is shown with the game's
  6-in-line headstock option (German UI: "6 in Reihe"). With the 3+3 option a seven-string guitar
  shows the same in-line headstock and pegs; six-string guitars keep the stock 3+3 headstock.
- Give the seventh string its own colour (light grey by default), with a separate colourblind setting.

### Setup and tools

- Load plain (unencrypted) PSARC charts with the built-in loader. Any CDLC enabler for the supported
  game build works; no special plain PSARC enabler build is needed.
- Avoid the freeze caused by clicking in the CDLC enabler's console. Console QuickEdit is turned off
  by default.
- Change settings and check status in the F9 overlay.
- Seven-string support runs on the native game layouts only. The switches of the older seven-string
  code are gone; an `rs7string.ini` that still has them loads, and the log lists each one as ignored
  (see [Removed settings](SETTINGS.md#removed-settings)). If a part of the seven-string support cannot
  start, rs7string leaves the game unchanged (six strings) and says why in `rs7string.log`.

### For chart authors

- Seven-string charts use the normal Lead and Rhythm arrangements. See the
  [charting guide](CHARTING.md) for tuning, notes, chords and techniques.
- Existing six-string charts are not converted. Eight-string charts are not supported yet.

See [INSTALL](INSTALL.md) for requirements, setup and known limitations, and [SETTINGS](SETTINGS.md)
for defaults.
