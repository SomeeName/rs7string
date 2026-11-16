# Changelog

**What is new in each rs7string version.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · **Changelog**

## 1.0.0-beta.4

**The biggest update so far: eight-string guitar, five- and six-string bass, local co-op with the extra strings and
very low tunings without workarounds. It also fixes the game getting stuck on the white loading screen.**

> [!NOTE]
> Updating from an older version? Follow the short [update steps](INSTALL.md#update). Your own `rs7string.ini` keeps
> working.

### New

- **Eight-string guitar.** Eight-string charts play with eight strings, with nothing to set up. Notes, chords,
  slides, bends and finger numbers all work on the eighth string, and the guitar gets a headstock with eight pegs.
  The eighth string is pink by default; you can change the colour in the F9 menu. Tunings are named like
  "F# Standard 8Str".
- **Five- and six-string bass.** A bass chart made for five strings (low B) or six strings (low B and high C) gets
  a real extended bass: its own strings, frets and a headstock with one peg per string. Notes on the extra strings
  are scored, the open low B included. The tuner starts on the low B, and the low B string is grey. The bass
  headstock is built from your own game files the first time you load such a song. Four-string bass songs are
  unchanged.
- **Local co-op.** Each player gets their own instrument. Player 2 can play a seven- or eight-string guitar or a
  five- or six-string bass, and each player's notes, tuner and scoring follow their own arrangement.
- **Low tunings without workarounds.** The game now listens as low as the lowest open string of the song's tuning,
  for bass and guitar. A five-string bass in A standard or drop G, or an eight-string guitar tuned down to B, no
  longer needs the A220 reference pitch trick or a separate low tuning fix. Songs in standard tunings are unchanged.
- **String spacing.** You can set how far apart the strings are for each extended instrument, for example to make a
  six-string bass as narrow as a four-string one. See `[neck] spacing_bass5`, `spacing_bass6`, `spacing_guitar7`
  and `spacing_guitar8` in the [settings](SETTINGS.md); `0`, the default, keeps the normal spacing.
- **More in the F9 menu.** It now also sets the eighth string's colours and how many notes a very wide chord may
  miss and still count.
- **Songs with many arrangements.** A song with more than five arrangements no longer crashes the Learn a Song
  page. The extra arrangements are in a list you can scroll with the arrow keys, the mouse wheel or Page Up / Page
  Down.

### Changes

- **The game keeps running if the mod hits a problem.** If a part of rs7string runs into an error while you play,
  that part is switched off instead of crashing the game, and `rs7string.log` gets a line starting with
  `hook fault:`. Please include that line in bug reports.
- **Fewer settings.** Settings that only changed internal values are gone, and the `rs7string.ini` in the release
  no longer lists developer and test keys. An old `rs7string.ini` still loads; the log names each key that is no
  longer used.
- Eight-string charts always play with eight strings, so the `[neck] eight` setting is gone.

<details>
<summary>The removed settings</summary>

`[neck] string7_v`, `fret_extend`, `fret_halflen`, `headstock_peg`, `eight`; `[color] ramp_swap`, `allow_inexact`;
`[chords] frame_scale`, `frame_axis`, `native_chordloops`; `[debug] head_stretch`, `head_stretch_mode`.
Chord bends, sustain techniques and chord frames on the seventh string now always use the game's own chord code.
An ini value longer than 259 characters is no longer cut short and used: the log names it and the setting is
ignored.

</details>

### Fixes

Starting the game:

- The game no longer sometimes gets stuck on the white loading screen at start.

On the highway and the neck:

- The glowing box and bars that show where your hand goes for a chord or note now cover every string on all
  extended instruments. They used to stop before the last string.
- Chord frames on the highway always cover all strings of a seven-string guitar.
- The song camera stays lined up with the highway on extended instruments, so the fret numbers stay on screen, also
  with reversed strings and left-handed layouts.
- The seventh string no longer keeps vibrating after a note ends, and a bend on it bends the right string.

Tuner and menus:

- Right-handed seven-string tuner: the outer pegs and the high e letter are no longer cut off.
- After closing and reopening the F9 menu, the first click outside it reaches the game again.
- Numbers saved from the F9 menu are written as shown, like `1.34`, not `1.33999...`.

Colours and other mods:

- Rocksmith Audio Bridge is recognised like RSMods, so its Extended Range low B colour and the string glow fix
  also apply with Audio Bridge installed.
- With RSMods custom string colours, the seventh string no longer loses its colour after many songs in one session.
- Local co-op with a guitar and a five- or six-string bass: the bass's low B notes and string are grey, and the
  guitar's B string keeps its own colour.

Headstocks:

- Headstock models built from your game files are rebuilt once when a new rs7string version ships a changed
  headstock. A model file you set yourself in `rs7string.ini` is never touched.
- An incomplete headstock build is repaired on the next start.

Stability:

- Many crash and glitch fixes from a full review of the code: starting the game, loading songs, graphics resets
  (such as Alt+Tab), the F9 menu, custom string spacing and co-op menus are all more robust.

<details>
<summary>Stability fixes in detail</summary>

- No crash when an extended guitar cannot be fully set up, or when its fret grid is still waiting to be expanded.
- Extended-bass profiles no longer hang after a logging failure.
- A rare startup timing problem could corrupt string colours or crash the game: rs7string now checks again right
  before it patches and leaves the game unpatched when the colours are already being built.
- If preparing a song's chords fails, the song still loads, without those chords.
- The highway floor finds its place again when the string positions only become ready after loading.
- The delayed tuner start covers every extended string.
- Extra-string colours apply consistently during play, and survive graphics resets, texture reloads and the F9
  menu opening.
- The F9 menu retries its start, gives the game its input back when it closes or fails, no longer leaves stale keys
  when reopened and no longer changes the selected arrangement while you navigate it.
- Patching the game is safer: a patch that cannot be written safely is left out instead of risked.
- Settings saved with a relative file name go to the same `rs7string.ini` they were read from.
- Custom string spacing keeps working after an internal error.
- Reopening co-op arrangement lists no longer leaks memory.
- Custom headstocks are kept in more cases: behind directory junctions, during builds and when they are named like
  temporary or cache files.

</details>

For chart makers:

- Chart tools: arrangement selection updates the matching tunings together, partly updated song data gets all
  missing guitar tuning offsets, raw PSARC data is kept and damaged compressed blocks are rejected. Release archives
  no longer include stray chord-diagram files.
- Docs: the documentation is now also a website with search,
  [someename.github.io/rs7string](https://someename.github.io/rs7string/), with clearer notes on headstock
  rebuilds, what bass charts need and which manifest keys set the string count.

### Known limits

- Local co-op with a guitar and a five- or six-string bass: the low B's string glow, fret wires, scale markers and
  chord diagram dots still use the guitar's B string colour.
- Local co-op, "Other arrangements" with more than eight arrangements: after scrolling, the song length in the
  header shows an extra digit ("1:001" for "1:00") until you leave the menu.
- Six-string bass in Riff Repeater: rarely, a note's tail is drawn in another string's colour. Pausing and resuming
  fixes it.

## 1.0.0-beta.3

### New

- Make the F9 menu easier for players: one tab bar (Seventh string, Guitar, Tuner, Menu, Advanced) opens on the
  everyday settings in plain words (string colours, guitar look, tuner, menu size and key), each with a `(?)` hint on
  hover and no scrolling. Technical settings are under the **Advanced** tab, and settings that need a game restart
  are marked `*`.
- Stop the tuner's wrong-instrument warning in seven-string Drop A, A Standard and G Standard. This is on by default
  (`[tuner] lowrange`); set it to `0` for the old behaviour.
- Use RSMods' Extended Range low B colour for the seventh string by default when that mode is enabled.
  Turn off `[color] rsmods_extended_range` to use your own normal and colourblind colours.
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
