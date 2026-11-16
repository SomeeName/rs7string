# rs7string settings

**Every `rs7string.ini` key, its default, and when a change takes effect.**

[README](../../README.md) · [Install](INSTALL.md) · **Settings** · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

> [!TIP]
> **Most players never need to change anything.** The defaults are the tested setup. Press **F9** in the game to
> change the common settings with a mouse; you only need this page for the details.

## The settings most people change

| I want to... | Setting | Where |
|---|---|---|
| Change the colour of the seventh or eighth string | `string6_color`, `string7_color` (and `_cb` for colourblind mode) | [Colour](#colour-color), F9 |
| Change how seven-string tunings are named (`B Standard 7Str`) | `name_prefix`, `name_suffix`, or `names=0` for stock names | [Tuner](#tuner-tuner), F9 |
| Make the neck narrower or wider | `spacing_guitar7`, `spacing_guitar8`, `spacing_bass5`, `spacing_bass6` | [Neck](#neck-neck), F9 |
| Be stricter about missed notes in big chords | `wide_misses=0` | [Chords](#chords-chords), F9 |
| Use another key for the overlay | `hotkey` | [Overlay](#overlay-overlay), F9 |
| Put the game on another monitor | `screen` | [Window](#window-window), F9 |

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [The settings most people change](#the-settings-most-people-change)
- [How settings work](#how-settings-work)
- Everyday settings: [Overlay](#overlay-overlay) · [Colour](#colour-color) · [Tuner](#tuner-tuner) ·
  [Chords](#chords-chords) · [Headstock files](#headstock-files-assets) ·
  [Plain packages](#plain-packages-psarc) · [Window](#window-window)
- Advanced settings: [Neck](#neck-neck) · [Debug](#debug-debug)
- [Removed settings](#removed-settings)

</details>

## How settings work

- `rs7string.ini` sits beside `dinput8.dll` in the game folder. The release ships it as a commented
  template with every key at its default.
- Missing keys use the defaults below.
- Switches are `0` (off) or `1` (on). A switch or whole-number setting with any other value, or with text that is
  not a number, is read as `0` (off) and logged. A decimal, text or colour setting with an invalid value uses its
  default instead. The overlay accepts only valid values.
- Exception: a few switches read once at startup, such as `[debug] log_redact` and `[window] console_quickedit_off`,
  count any nonzero number as on.
- Edit the file with the game closed, then start Rocksmith.
- Press **F9** to open the overlay. It has one tab each for colours, guitar look, tuner and menu; hover the
  `(?)` for a hint. Everything else is under the **Advanced** tab, with the key names.
- The `string<N>_*` colour keys count strings from 0, the way the game does: `string6_*` is the seventh string
  and `string7_*` the eighth.
- Settings marked `*` in the overlay apply after you restart the game; the overlay also says when a
  change is waiting for a restart. **Save to rs7string.ini** writes only the keys you changed and
  keeps the other keys and comments.

**Columns:**

| Column | Meaning |
|---|---|
| Apply | **Live**: takes effect at once when changed in the overlay (editing the file during play is not guaranteed to reload it). **Restart**: restart Rocksmith. **Next song**: read again at each song start. **Next build**: used the next time the game builds the instrument, which is when a song needs another instrument or string count than the last one, or after a restart. |
| F9 | `yes`: the overlay has a control for this key. `no`: set it in the file. |
| Exp. | `yes`: a diagnostic, a test tool, or a feature still under test (some are on by default). `no`: the default is the normal behaviour. |

> [!NOTE]
> The sections below are in a reading order. In `rs7string.ini` the sections are in this order:
> `[overlay]`, `[debug]`, `[color]`, `[neck]`, `[assets]`, `[chords]`, `[tuner]`, `[psarc]`,
> `[window]`.

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
| `string6_color` | `C8C8C8` | Colour of the seventh string as `RRGGBB` (`#` optional): gems, sparks, hit/miss text, string line, glow and tuner. Also the low B of a five- or six-string bass. Invalid text falls back to the default and is logged. | Live | yes | no |
| `string6_color_cb` | same as `string6_color` | The same colour in colourblind mode. When omitted or empty it follows `string6_color` at startup. After that the overlay keeps a separate value. | Live | yes | no |
| `string7_color` | `FF66CC` | Colour of the eighth string as `RRGGBB`: gems, string line, glow and sparks. If the game's colour texture has no room for the extra colour bands, its gems fall back to string 5's colour. Invalid text falls back to the default and is logged. | Live | yes | no |
| `string7_color_cb` | `FF66CC` | The same in colourblind mode. | Live | yes | no |

## Tuner: `[tuner]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `lowrange` | `1` | In seven-string tuner sessions, the wrong-instrument warning and the lowest detected open note follow the arrangement's lowest note, so Drop A, A Standard and G Standard do not warn. | Live | yes | no |
| `names` | `1` | Shows tuning names for extended instruments (for example `B Standard 7Str`, `F# Standard 8Str`, `B Standard 5Str`) in the song list, song pages and tuner. `0` shows the stock names. | Restart | yes | no |
| `name_prefix` | empty | Text before an extended tuning name (`7Str` gives `7Str B Standard`). Printable ASCII, at most 31 characters; invalid text uses the default. | Restart | yes | no |
| `name_suffix` | `7Str` | Text after an extended tuning name. The first `7` in it becomes the instrument's string count, so `7Str` shows `8Str` on an eight-string guitar and `5Str` on a five-string bass. Empty means no suffix on seven strings. Printable ASCII, at most 31 characters; invalid text uses the default. | Restart | yes | no |

Examples (B standard on seven strings, F# standard on eight, B standard on five-string bass):

| `name_prefix` | `name_suffix` | 7-string | 8-string | 5-string bass |
|---|---|---|---|---|
| empty | `7Str` (default) | `B Standard 7Str` | `F# Standard 8Str` | `B Standard 5Str` |
| empty | `7-string` | `B Standard 7-string` | `F# Standard 8-string` | `B Standard 5-string` |
| `ERG` | `7` | `ERG B Standard 7` | `ERG F# Standard 8` | `ERG B Standard 5` |
| empty | empty | `B Standard` | `F# Standard 8Str` | `B Standard 5Str` |

A suffix without a `7` keeps the default mark on eight-string guitars and basses, as in the last row.

Tuning names are English. Tuning filters and sorting still use the six-string tuning.

## Chords: `[chords]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `wide_misses` | `2` | How many notes may be missing when a seven- or eight-note chord is matched, on any string (0-2). Five- and six-note chords allow one miss; two- to four-note chords allow none, as in the stock game. | Live | yes | no |
| `diagrams7` | `1` | Chord diagrams with a row for the seventh (and eighth) string. `1` uses the recipes in the `chord-diagrams` folder to patch the game's diagram movies in memory (September 2022 build only; a movie that does not match keeps the stock diagrams). `0` turns it off, and the folder is not read. | Restart | no | yes |

## Headstock files: `[assets]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `headstock7` | `1` | Shows the mod's headstocks: the seven- and eight-peg guitar headstocks and the five- and six-string bass headstocks. `0` keeps the stock headstock on every instrument. | Restart | yes | no |
| `selfbuild` | `1` | Builds missing headstock models from your own game into `dir` at first use. A changed package also rebuilds the default models. A build replaces generated files whose contents differ. A failed build keeps the stock headstock. `0` turns off all headstock builds. Needs the four `rs7string_headstock*.bin` files next to the DLL ([install guide](INSTALL.md#3-headstock-built-on-first-use)). | Restart | no | yes |
| `headstock7_file` | `rs7_inline7.nif` | Secondary model for the right-handed in-line headstock, shown on seven-string guitars. A relative name is taken from `dir`. An empty value, `none`, `off` or `0` disables this secondary model. It does not turn off the builder. | Restart | yes | no |
| `headstock7_file_lh` | `rs7_inline7_lh.nif` | Secondary model for the left-handed in-line headstock, shown on seven-string guitars. Same rules as `headstock7_file`. | Restart | no | no |
| `headstock8_file` | `rs7_inline8.nif` | Primary model for the right-handed in-line headstock, with eight pegs. A relative name is taken from `dir`. An empty value, `none`, `off` or `0` uses only the `dir` + `suffix` rule and turns off the guitar headstock builder for this hand. | Restart | yes | no |
| `headstock8_file_lh` | `rs7_inline8_lh.nif` | Primary model for the left-handed in-line headstock, with eight pegs. Same rules as `headstock8_file`. | Restart | no | no |
| `headstock7_wood` | `0.6` | Brightness of the model's wood texture (`1` = as built). Valid 0.05-1; any other value uses `0.6`. | Restart | yes | no |
| `dir` | `rs7string\headstock\` | Folder for the built models and for edited models named `<dir>\<stock base><suffix>.nif`. A relative path is taken from the DLL's folder (the game folder). | Restart | yes | no |
| `suffix` | `_7inline` | File-name suffix of edited models. A value containing `\`, `/`, `:` or `.` falls back to `_7inline`. | Restart | yes | no |

See [INSTALL](INSTALL.md#3-headstock-built-on-first-use) for the headstock builder and its log
lines. Keep custom models and textures under other file names, and point the model keys at them so a rebuild
does not replace them. Never share generated `.nif` files.

## Plain packages: `[psarc]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `plain` | `1` | Loads plain (unencrypted) PSARC charts with any CDLC enabler for the supported game build. You still need the enabler; no special plain PSARC build is required. If the enabler already handles plain packages, rs7string leaves that alone. `0` turns off rs7string's plain-package loader. | Restart | no | no |

## Window: `[window]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `console_quickedit_off` | `1` | Turns off QuickEdit in the CDLC enabler's console, so a click there can no longer freeze the game on the loading screen or during play. Choosing Edit > Mark in the console menu can still freeze it; press Enter in the console to end the selection. `0` leaves QuickEdit unchanged. | Restart | no | no |
| `screen` | `0` | Puts the game window on monitor N: the primary monitor first, then the others in Windows order (not the Windows display numbers). `0`, or a monitor that does not exist, keeps the stock placement. The overlay allows 0-16. | Restart | yes | no |

## Fixes: `[fixes]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `menu_retry` | `1` | Prevents a menu from staying on the white loading screen when its menu file was requested before the game finished indexing its files at startup. The request is sent again as soon as that file is indexed (if that is never seen, every 5 seconds for up to 30 seconds). `0` turns this fix off. If another mod already changed these loading functions, rs7string leaves them alone. | Restart | no | no |

## Neck: `[neck]`

> [!WARNING]
> Advanced. Keep these at their defaults unless you are adjusting visuals.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `string7_line` | `1` | Shows the extra string line. `0` hides it. | Restart | yes | no |
| `spacing_bass5` | `0` | Distance between the strings of a five-string bass on the neck and the note highway. `0` keeps the normal bass spacing (2.68); `2.01` makes the neck as wide as a four-string bass, `1.608` is the guitar spacing. Valid 0.1-4. The headstock narrows with it. Applies the next time the bass is built (a song with another instrument or string count, or a restart). | Next build | yes | no |
| `spacing_bass6` | `0` | The same for a six-string bass. At the normal bass spacing its neck is two thirds wider than a guitar's; `1.608` (the guitar spacing) makes it as wide as a four-string bass. | Next build | yes | no |
| `spacing_guitar7` | `0` | The same for a seven-string guitar. `0` keeps the guitar spacing (1.608); `1.34` makes the neck as wide as a six-string guitar. | Next build | yes | no |
| `spacing_guitar8` | `0` | The same for an eight-string guitar. `0` keeps the guitar spacing (1.608); `1.15` makes the neck as wide as a six-string guitar. | Next build | yes | no |

Extended bass (five- and six-string) has no setting: it turns on only for a bass arrangement whose
chart marks itself as five- or six-string bass (an RS7X block with instrument 1, see [RS7X-SPEC.md](RS7X-SPEC.md)).
The bass is then built with five or six strings and gets its own headstock (`rs7_bass5*.nif` / `rs7_bass6*.nif` in
the headstock folder). Four-string bass and guitar charts are not affected.

## Debug: `[debug]`

> [!WARNING]
> For diagnosis only. Leave these at their defaults for normal play.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `diagnostics` | `0` | `1` turns on extra diagnostic logging for bug reports. Some parts only take effect after a restart. | Live (some parts: Restart) | yes | yes |
| `song_end_log` | `1` | Logs how each song ended (for bug reports). | Live | yes | no |
| `log_redact` | `1` | `rs7string.log` hides your Windows user name (`<user>`), your user folder (`%USERPROFILE%`) and your computer name (`<pc>`). `0` writes them as they are; only for your own debugging, never post such a log. | Restart | no | no |

Developer and test keys (test driving, bisection, extra traces) are not part of the release template;
[DEVELOPMENT.md](../DEVELOPMENT.md#developer-and-test-settings) lists them.

## Removed settings

Version 1.0 removed these switches and the settings that only changed internal values. An `rs7string.ini` that still has them loads normally: each one is ignored,
and the log says `ignored, removed in 1.0`.

- `[debug] string6_as_string0`
- `[neck] native_colours`
- `[neck] native_tuner`
- `[tuner] seven_slot`
- `[debug] tuner_trace`
- `[neck] native_pegset`, `peg7_k`, `peg7_offset`
- `[neck] native_strings`, `string7_draw_fix`
- `[debug] string7_gpu_diag`
- `[neck] native_nextgrid`, `native_fretgrid`, `native_pitch`, `native_publish`, `clite`
- `[neck] eight` (eight strings are always on)
- `[neck] string7_v`, `fret_extend`, `fret_halflen`, `headstock_peg` (always the tested values)
- `[color] ramp_swap`, `allow_inexact` (the gem colour copy always runs, without DXT ramps)
- `[chords] frame_scale`, `frame_axis` (the chord frame follows the lane positions)
- `[chords] native_chordloops` (chord bends, sustain techniques and chord frames always cover strings 7 and 8)
- `[debug] head_stretch`, `head_stretch_mode` (a headstock test)
