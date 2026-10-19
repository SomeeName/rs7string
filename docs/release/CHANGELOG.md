# Changelog

**What is new in each rs7string version.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · **Changelog**

## 1.0.0-beta.3

### New

- Make the F9 menu easier for players: one tab bar (Seventh string, Guitar, Tuner, Menu, Advanced) opens on the
  everyday settings in plain words (string colours, guitar look, tuner, menu size and key), each with a `(?)` hint on
  hover and no scrolling. Technical settings are under the **Advanced** tab, and settings that need a game restart
  are marked `*`.
- Stop the tuner's wrong-instrument warning in seven-string Drop A, A Standard and G Standard. This is on by default
  (`[tuner] lowrange`); set it to `0` for the old behaviour.
- Use RSMods' Extended Range low B colour for the seventh string by default when that mode is enabled.
  Turn off `color.rsmods_extended_range` to use your own normal and colourblind colours.
- Keep personal data out of `rs7string.log` by default: your Windows user name, user folder and computer name are
  replaced by `<user>`, `%USERPROFILE%` and `<pc>`, so the log can be shared as it is. `[debug] log_redact=0` turns
  it off.

### Fixes

- Stop clicks, the mouse wheel and typing in the F9 menu from also reaching the game behind it.
- Show seventh-string notes in the seventh string's colour when RSMods custom string colours or
  Extended Range are on. They were grey or white before.
- Fix RSMods custom, Extended Range and rainbow string colours addressing the wrong colours
  with rs7string, including colourblind mode, headstock pegs and fret numbers.
- Detect RSMods' Extended Range colour before its proxy DLL loads, including when Windows' XInput DLL is also loaded.
- Give the selected tuner peg of a 3+3 headstock a glow in the shape of the in-line peg that is
  shown. It followed the hidden stock peg before.
- Keep the seventh peg's tuner glow in the seventh string's colour. Bright colours such as RSMods'
  Extended Range low B turned cyan.
- Keep the selected tuner peg and its coloured glow in sync when RSMods applies Extended Range colours.
- Keep the seven-string tuner on low B when entering from a mode that selects the starting string twice.
- Show the glow outline of a pure red string (such as a low E of `800000`) with RSMods custom string
  colours. RSMods gave it a near-black glow, so the outline was invisible.
- Mark double stops like the stock game in songs whose chord data rs7string did not convert.
- Bend the seventh and eighth strings toward the neck's middle when plucked, like the low E.

### For testers

- Add test-only settings `force_colourblind`, `force_lefty` and `force_headstock` for automated in-game tests.
  They change only the running game; your profile keeps its own settings.

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
