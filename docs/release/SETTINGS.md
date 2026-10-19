# rs7string settings

**Every `rs7string.ini` key, its default, and when a change takes effect.**

[README](../../README.md) · [Install](INSTALL.md) · **Settings** · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

> [!TIP]
> Most players never need to change anything. The defaults are the tested setup. The keys you are
> most likely to want are the [seventh-string colour](#colour-color) and the
> [tuning names](#tuner-tuner).

## Contents

- [How settings work](#how-settings-work)
- Everyday settings: [Overlay](#overlay-overlay) · [Colour](#colour-color) · [Tuner](#tuner-tuner) ·
  [Chords](#chords-chords) · [Headstock files](#headstock-files-assets) ·
  [Plain packages](#plain-packages-psarc) · [Window](#window-window)
- Advanced settings: [Neck](#neck-neck) · [Debug](#debug-debug) · [Virtual guitarist](#virtual-guitarist-test)
- [Removed settings](#removed-settings)

## How settings work

- `rs7string.ini` sits beside `dinput8.dll` in the game folder. The release ships it as a commented
  template with every key at its default.
- Missing keys use the defaults below.
- Switches are `0` (off) or `1` (on). At startup any nonzero number counts as on; the overlay accepts
  only `0` or `1`.
- Edit the file with the game closed, then start Rocksmith.
- Press **F9** to open the overlay. It shows the everyday settings in plain words: the seventh-string
  colours, the RSMods Extended Range colour, the guitar's look, the tuner and tuning names, and the menu
  size and key, one tab each (hover the `(?)` for a hint). Everything else is under the **Advanced** tab, with
  the key names.
- Settings marked `*` in the overlay apply after you restart the game; the overlay also says when a
  change is waiting for a restart. **Save to rs7string.ini** writes only the keys you changed and
  keeps the other keys and comments.

**Columns:**

| Column | Meaning |
|---|---|
| Apply | **Live**: takes effect at once when changed in the overlay (editing the file during play is not guaranteed to reload it). **Restart**: restart Rocksmith. **Next song**: read again at each song start. |
| F9 | `yes`: the overlay has a control for this key. `no`: set it in the file. |
| Exp. | `yes`: a diagnostic, a test tool, or a feature still under test (some are on by default). `no`: the default is the normal behaviour. A *legacy* note marks an older path that only runs when another switch is `0`. |

> [!NOTE]
> The sections below are in a reading order. In `rs7string.ini` the sections are in this order:
> `[overlay]`, `[debug]`, `[color]`, `[neck]`, `[assets]`, `[chords]`, `[tuner]`, `[psarc]`,
> `[window]`, `[test]`.

## Overlay: `[overlay]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `enabled` | `1` | Installs the overlay. `0` installs none of its hooks. | Restart | yes | no |
| `hotkey` | `F9` | Key that shows or hides the overlay: F1-F24, INSERT, HOME, END, PAUSE, SCROLL, A-Z, 0-9, or `0xNN`. | Restart | yes | no |
| `open` | `0` | `1` shows the overlay from the first frame. | Restart | yes | no |
| `scale` | `0` | `0` sets the scale from the window height (height / 720, limited to 1-3). A positive value is a fixed scale, up to 4. | Live | yes | no |

## Colour: `[color]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `rsmods_extended_range` | `1` | Follow RSMods Extended Range low B colour when RSMods is installed and `ExtendedRange=on`: standard modes use `00C68E`, custom mode uses valid `[String Colors] string0_CB`. Applies in both colour modes. RSMods settings are read at startup. `0` uses the two colours below. Their saved values stay intact. Missing or invalid RSMods settings keep your own colours. | Live | yes | no |
| `string6_color` | `C8C8C8` | Colour of the seventh string as `RRGGBB` (`#` optional): gems, sparks, hit/miss text, string line, glow and tuner. Invalid text falls back to the default and is logged. | Live | yes | no |
| `string6_color_cb` | same as `string6_color` | The same colour in colourblind mode. When omitted or empty it follows `string6_color` at startup. After that the overlay keeps a separate value. | Live | yes | no |
| `ramp_swap` | `1` | Lets seventh-string gems take their own colour (copies the game's colour ramps with an extra band). `0` turns this off. | Restart | yes | no |
| `allow_inexact` | `0` | Also copies compressed (DXT1/DXT5) ramps. They are decoded on the CPU, so colours may round differently. | Restart | yes | yes |

## Tuner: `[tuner]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `lowrange` | `1` | In seven-string tuner sessions, the wrong-instrument warning and the lowest detected open note follow the arrangement's lowest note, so Drop A, A Standard and G Standard do not warn. | Live | yes | yes |
| `names` | `1` | Shows seven-string tuning names (for example `B Standard`) in the song list, song pages and tuner. `0` shows the stock names. | Restart | yes | no |
| `name_prefix` | empty | Text before a seven-string tuning name (`7Str` gives `7Str B Standard`). Printable ASCII, at most 31 characters; invalid text uses the default. | Restart | yes | no |
| `name_suffix` | `7Str` | Text after a seven-string tuning name (`B Standard 7Str`). Empty means no suffix. Printable ASCII, at most 31 characters; invalid text uses the default. | Restart | yes | no |

Tuning names are English. Tuning filters and sorting still use the six-string tuning.

## Chords: `[chords]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `wide_misses` | `2` | How many notes may be missing when a seven- or eight-note chord is matched, on any string (0-2). Five- and six-note chords allow one miss; two- to four-note chords allow none, as in the stock game. | Restart | no | no |
| `diagrams7` | `1` | Seven-row chord diagrams. `1` uses the recipes in the `chord-diagrams` folder to patch the game's diagram movies in memory (September 2022 build only; a movie that does not match keeps the stock diagrams). `0` turns it off, and the folder is not read. | Restart | no | yes |
| `native_chordloops` | `1` | Chord techniques on the seventh string: chord bends, sustain techniques and seven-string chord frames. `0` keeps the legacy chord code. | Restart | no | no |
| `frame_scale` | `1.134` | No longer used. The chord frame now follows the guitar's lane positions. An old value is read, logged and ignored. | Restart | no | no |
| `frame_axis` | `1` | Axis (0-2) of the chord frame span; invalid values fall back to `1`. The effect of 0 and 2 is untested; keep `1`. | Restart | no | no |

## Headstock files: `[assets]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `headstock7` | `1` | Shows the seven-string headstock on seven-string guitars. `0` keeps the stock headstock. | Restart | yes | no |
| `selfbuild` | `1` | Runs the headstock builder: when the model file is missing, builds it from your own game at the first guitar load into `dir`. Later starts just load it. Existing files are never overwritten. A failed build keeps the stock headstock. Needs `rs7string_headstock.bin` next to the DLL; left-handed builds need both `.bin` files. | Restart | no | yes |
| `headstock7_file` | `rs7_inline7.nif` | Model file for the right-handed in-line headstock. A relative name is taken from `dir`. An empty value, `none`, `off` or `0` uses only the `dir` + `suffix` rule and turns off the headstock builder for this hand. | Restart | yes | no |
| `headstock7_file_lh` | `rs7_inline7_lh.nif` | Model file for the left-handed in-line headstock. Same rules as `headstock7_file`. | Restart | no | no |
| `headstock7_wood` | `0.6` | Brightness of the model's wood texture (`1` = as built). The file accepts values above 0.05 up to 1 (otherwise 0.6); the overlay allows 0.05-1. | Restart | yes | no |
| `dir` | `rs7string\headstock\` | Folder for the built models and for edited models named `<dir>\<stock base><suffix>.nif`. A relative path is taken from the DLL's folder (the game folder). | Restart | yes | no |
| `suffix` | `_7inline` | File-name suffix of edited models. A value containing `\`, `/`, `:` or `.` falls back to `_7inline`. | Restart | yes | no |

See [INSTALL](INSTALL.md#3-headstock-built-on-first-start) for the headstock builder and its log
lines. Never share generated `.nif` files.

## Plain packages: `[psarc]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `plain` | `1` | Loads plain (unencrypted) PSARC charts with any CDLC enabler for the supported game build. You still need the enabler; no special plain PSARC build is required. If the enabler already handles plain packages, rs7string leaves that alone. `0` turns off rs7string's plain-package loader. | Restart | no | no |

## Window: `[window]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `console_quickedit_off` | `1` | Turns off QuickEdit in the CDLC enabler's console, so a click there can no longer freeze the game on the loading screen or during play. Choosing Edit > Mark in the console menu can still freeze it; press Enter in the console to end the selection. `0` leaves QuickEdit unchanged. | Restart | no | no |
| `screen` | `0` | Puts the game window on monitor N: the primary monitor first, then the others in Windows order (not the Windows display numbers). `0`, or a monitor that does not exist, keeps the stock placement. The overlay allows 0-16. | Restart | yes | no |
| `background` | `0` | Advanced test mode: the game window never activates itself, and test keys can be sent through the `\\.\pipe\rs7string` pipe. Leave `0` for normal play. | Restart | yes | yes |

## Neck: `[neck]`

> [!WARNING]
> Advanced. Keep these at their defaults unless you are adjusting visuals or diagnosing a problem.
> Most `native_*` switches must all stay `1` for seven-string mode to work as tested.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `string7_line` | `1` | Shows the extra string line. `0` hides it. | Restart | yes | no |
| `string7_v` | `0.4` | Texture row for the seventh string's line: 0.4-0.9 are guitar strings 0-5, 0.0-0.3 are bass strings. Invalid values fall back to `0.4`. | Live | yes | no |
| `fret_extend` | `1` | On seven-string songs, moves and scales the fret bars so they span the seventh string. `0` keeps the stock bars. | Live | yes | no |
| `fret_halflen` | `4.61` | Half-length L in the bar scale k = (L + d/2) / L; valid 1-20. | Live | yes | no |
| `headstock_peg` | `1` | Shows the seventh tuning peg on the seven-string headstock. `0` shows six pegs. | Restart | yes | no |
| `eight` | `0` | Experimental eight-string preparation. Eight-string play is not supported in this release. | Restart | no | yes |

## Debug: `[debug]`

> [!WARNING]
> For diagnosis and testing only. Leave these at their defaults for normal play.

<details>
<summary><b>Show the debug keys</b></summary>

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `diagnostics` | `0` | `1` turns on the D1/D4 loggers at once and, if set at startup, the D2/D3/D5 diagnostic patches. | Live (D2/D3/D5: Restart) | yes | yes |
| `tuner_autotune` | `0` | Testing without a guitar: each tuner string reports tuned N seconds (1-30) after it is selected. `0` is off. | Live | yes | yes |
| `song_end_log` | `1` | Logs which route ended each song. | Live | yes | no |
| `head_cull` | empty | Keeps the named headstock frame node hidden on seven-string guitars. Empty is off. | Live | yes | yes |
| `head_stretch` | `1` | Stretches the headstock lengthwise about the nut by this factor (0.5-2). `1` is off. | Live | yes | yes |
| `head_stretch_mode` | `0` | Stretch method: `0` scales the length axis only; `1` also scales the entity. | Live | yes | yes |
| `veh_log` | `0` | Logs the first 40 exceptions, plus a separate budget of 40 exceptions in the game, through a vectored exception handler. | Restart | no | yes |
| `skip_sites` | empty | Test bisection: comma-separated site IDs are resolved but not written at startup. Leave empty. | Restart | no | yes |
| `alloc_stats` | `0` | Logs the DLL's live/total allocation counts at song start and end (`allocstats` lines). | Restart | no | yes |
| `diagrams_trace` | `0` | Logs chord-diagram recipe loading and movie swaps. Only used with `[chords] diagrams7=1`. | Restart | no | yes |
| `spin_probe` | `0` | Starts a watchdog that samples busy threads (suspends and resumes them briefly). | Restart | no | yes |
| `plain_delay_ms` | `0` | Test only: starts rs7string's plain PSARC patches this many milliseconds late (at most 30000), so a CDLC enabler with its own plain PSARC support patches first. rs7string then logs `provided by the CDLC enabler`. | Restart | no | yes |
| `log_redact` | `1` | `rs7string.log` hides your Windows user name (`<user>`), your user folder (`%USERPROFILE%`) and your computer name (`<pc>`). `0` writes them as they are; only for your own debugging, never post such a log. | Restart | no | yes |
| `publish_trace` | `0` | Logs the readiness waits of the native layouts while a seven- or eight-string guitar is built. | Restart | no | yes |

</details>

## Virtual guitarist: `[test]`

> [!WARNING]
> Test only. Keep `vguitar=0` for real playing. With `vguitar_mix=0` the virtual guitarist replaces
> your real input during a song.

<details>
<summary><b>Show the virtual guitarist keys</b></summary>

The virtual guitarist plays the chart's notes into the game's audio input, so a chart can be tested
without a guitar. It needs an open audio-input device. It writes `rs7string_vguitar.csv` beside the
DLL and summaries to the log. Synthetic hits do not prove that a chart plays well on a real guitar.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `vguitar` | `0` | Enables the virtual guitarist. | Restart | no | yes |
| `vguitar_mode` | `chart` | Pitch source: `chart` (chart pitch), `game` (the game's target), `midi:N` (a fixed MIDI note, 20-100) or `off` (no notes). | Next song | no | yes |
| `vguitar_filter` | `all` | Notes to play: `all`, strings such as `6` or `056`, or string and fret such as `6:0` (fret 0-30). Chords are played only with `all`. String 7 needs `[neck] eight=1`. | Next song | no | yes |
| `vguitar_lead_ms` | `0` | Onset advance in milliseconds (-500 to 500). A positive value plays the notes earlier. | Next song | no | yes |
| `vguitar_gain` | `0.3` | Level of the synthesised pluck, 0-1. | Next song | no | yes |
| `vguitar_mix` | `0` | `0` replaces the real input during a song; `1` adds the synthesised notes to it. | Next song | no | yes |
| `vguitar_trace` | `0` | Logs the detector frame for 0.6 s after each synthesised onset. | Next song | no | yes |
| `vguitar_chord6` | `play` | Seventh-string note of a chord: `play`; `omit` drops it; `only` plays only that note; `+N` / `-N` shifts it by N semitones (1-24); `omitlow` / `omithigh` drop the chord's lowest or highest note. | Next song | no | yes |

</details>

## Test speed-up: `[test]`

> [!WARNING]
> Test only, for automated in-game tests. Keep `fast=0` for real playing: with `fast=1` a song ends on its
> song page and the results screen is not shown.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `fast` | `0` | A song ends on its song page instead of the results screen. | Restart | no | yes |
| `force_colourblind` | `0` | `1`: colourblind mode for this session. Your profile keeps its own setting. | Restart | no | yes |
| `force_lefty` | `0` | `1`: left-handed guitar for this session. Your profile keeps its own setting. | Restart | no | yes |
| `force_headstock` | (empty) | `33` (3+3) or `6` (6 in-line): headstock type for this session; empty: your profile's. | Restart | no | yes |

> [!NOTE]
> Keys without an overlay control can still be changed in the file. Which keys stay public in later
> versions is still to be decided.

## Removed settings

Version 1.0 removed the switches of the older seven-string code. An `rs7string.ini` that still has
them loads normally: each one is ignored, and the log says `ignored, removed in 1.0`.

- `[debug] string6_as_string0`
- `[neck] native_colours`
- `[neck] native_tuner`
- `[tuner] seven_slot`
- `[debug] tuner_trace`
- `[neck] native_pegset`, `peg7_k`, `peg7_offset`
- `[neck] native_strings`, `string7_draw_fix`
- `[debug] string7_gpu_diag`
- `[neck] native_nextgrid`, `native_fretgrid`, `native_pitch`, `native_publish`, `clite`
