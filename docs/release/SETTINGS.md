# RS Extended Range settings

**Every `rsxr.ini` key, its default, and when a change takes effect.**

To keep settings from 1.0.0-beta.4 or earlier, rename `rs7string.ini` to `rsxr.ini`
([update steps](INSTALL.md#updating-from-100-beta4-or-earlier)).

[README](../../README.md) · [Install](INSTALL.md) · **Settings** · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

> [!TIP]
> **Most players never need to change anything.** The defaults are the tested setup. Press **F9** in the game to
> change the common settings with the mouse. You need this page only for the details.

## The settings most people change

| I want to... | Setting | Where |
|---|---|---|
| Change the color of the seventh or eighth string | `string6_color`, `string7_color`, plus `_cb` for colorblind mode | [Color](#color-color), F9 |
| Change how seven-string tunings are named (`B Standard 7Str`) | `name_prefix`, `name_suffix`, or `names=0` for stock names | [Tuner](#tuner-tuner), F9 |
| Make the neck narrower or wider | `spacing_guitar7`, `spacing_guitar8`, `spacing_bass5`, `spacing_bass6` | [Neck](#neck-neck), F9 |
| Be stricter about missed notes in big chords | `wide_misses=0` | [Chords](#chords-chords), F9 |
| Use another key for the overlay | `hotkey` | [Overlay](#overlay-overlay), F9 |
| Put the game on another monitor | `screen` | [Window](#window-window), F9 |

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [The settings most people change](#the-settings-most-people-change)
- [How settings work](#how-settings-work)
- Everyday settings: [Overlay](#overlay-overlay) · [Color](#color-color) · [Tuner](#tuner-tuner) ·
  [Chords](#chords-chords) · [Headstock files](#headstock-files-assets) ·
  [Plain packages](#plain-packages-psarc) · [Window](#window-window) · [Fixes](#fixes-fixes)
- Advanced settings: [Neck](#neck-neck) · [Debug](#debug-debug)
- [Removed settings](#removed-settings)

</details>

## How settings work

- `rsxr.ini` is next to `dinput8.dll` in the game folder. The release ships it as a template with comments and
  every key at its default.
- A key that is missing from the file uses the default below.
- Switches are `0` (off) or `1` (on). If a switch or whole-number setting has any other value, or text that is
  not a number, the mod reads it as `0` (off) and writes a line to the log. If a decimal, text or color setting has
  an invalid value, the mod uses its default instead. The overlay accepts only valid numbers and switches. If the
  overlay does not know a `hotkey` name, it falls back to F9 at the next start.
- A few switches are an exception. The mod reads them once at startup, and they count any nonzero number as on.
  Examples are `[debug] log_redact` and `[window] console_quickedit_off`.
- Edit the file while the game is closed, then start Rocksmith.
- Press **F9** to open the overlay. It has one tab each for colors, guitar look, tuner and menu. Hover over the
  `(?)` to see a hint. Everything else is under the **Advanced** tab, with the key names.
- The `string<N>_*` color keys count strings from 0, the way the game does. So `string6_*` is the seventh string
  and `string7_*` is the eighth.
- Settings marked `*` in the overlay apply after you restart the game. The overlay also tells you when a
  change is waiting for a restart. **Save to rsxr.ini** writes only the keys you changed and
  keeps the other keys and comments.

The tables below use these columns.

| Column | Meaning |
|---|---|
| Apply | **Live**: takes effect at once when you change it in the overlay. If you edit the file during play, the mod may not reload it. **Restart**: restart Rocksmith. **Next song**: the mod reads it again at the start of each song. **Next build**: used the next time the game builds the instrument. That happens when a song needs a different instrument or string count than the last one, or after a restart. |
| F9 | `yes`: the overlay has a control for this key. `no`: set it in the file. |
| Exp. | `yes`: a diagnostic, a test tool, or a feature still under test (some are on by default). `no`: the default is the normal behavior. |

> [!NOTE]
> The sections below are in the order that is easiest to read. In `rsxr.ini`, the sections are in this order:
> `[overlay]`, `[debug]`, `[color]`, `[neck]`, `[assets]`, `[chords]`, `[tuner]`, `[psarc]`,
> `[window]`, `[fixes]`.

## Overlay: `[overlay]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `enabled` | `1` | Installs the overlay. `0` installs none of its hooks. | Restart | yes | no |
| `hotkey` | `F9` | The key that shows or hides the overlay: F1-F24, INSERT, DELETE, HOME, END, PGUP, PGDN, PAUSE, SCROLL, NUMLOCK, OEM_3, A-Z, 0-9, or a virtual-key code from `0x01` to `0xFE`. | Restart | yes | no |
| `open` | `0` | `1` shows the overlay from the first frame. | Restart | yes | no |
| `scale` | `0` | `0` sets the scale from the window height (height / 720, limited to 1-3). A positive value is a fixed scale from 0.5 to 4. A smaller value counts as 0.5. | Live | yes | no |

## Color: `[color]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `rsmods_extended_range` | `1` | Uses the RSMods Extended Range low B color when RSMods is installed and `ExtendedRange=on`. The standard modes use `00C68E`. The custom mode uses `[String Colors] string0_CB` if it is valid. This applies in both color modes. The mod reads the RSMods settings at startup. `0` uses the two colors below, and their saved values stay intact. If the RSMods settings are missing or invalid, your own colors stay. | Live | yes | no |
| `string6_color` | `C8C8C8` | Color of the seventh string as `RRGGBB`, with an optional `#`. It's used for gems, sparks, hit/miss text, string line, glow and tuner, and for the low B of a five- or six-string bass. Invalid text falls back to the default and is logged. | Live | yes | no |
| `string6_color_cb` | same as `string6_color` | The same color in colorblind mode. If the key is missing or empty, it follows `string6_color` at startup. After that, the overlay keeps a separate value. | Live | yes | no |
| `string7_color` | `FF66CC` | Color of the eighth string as `RRGGBB`, used for gems, string line, glow and sparks. If the game's color texture has no room for the extra color bands, the eighth string's gems fall back to string 5's color. Invalid text falls back to the default and is logged. | Live | yes | no |
| `string7_color_cb` | `FF66CC` | The same color in colorblind mode. | Live | yes | no |

## Tuner: `[tuner]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `lowrange` | `1` | In seven-string tuner sessions, the wrong-instrument warning and the lowest detected open note follow the arrangement's lowest note, so Drop A, A Standard and G Standard do not warn. | Live | yes | no |
| `names` | `1` | Shows tuning names for extended instruments (for example `B Standard 7Str`, `F# Standard 8Str`, `B Standard 5Str`) in the song list, song pages and tuner. `0` shows the stock names. | Restart | yes | no |
| `name_prefix` | empty | Text before an extended tuning name (`7Str` gives `7Str B Standard`). Printable ASCII, at most 31 characters. Invalid text uses the default. | Restart | yes | no |
| `name_suffix` | `7Str` | Text after an extended tuning name. The first `7` in it becomes the instrument's string count, so `7Str` shows `8Str` on an eight-string guitar and `5Str` on a five-string bass. Empty means no suffix on seven strings. Printable ASCII, at most 31 characters. Invalid text uses the default. | Restart | yes | no |

These examples use B standard on seven strings, F# standard on eight and B standard on a five-string bass.

| `name_prefix` | `name_suffix` | 7-string | 8-string | 5-string bass |
|---|---|---|---|---|
| empty | `7Str` (default) | `B Standard 7Str` | `F# Standard 8Str` | `B Standard 5Str` |
| empty | `7-string` | `B Standard 7-string` | `F# Standard 8-string` | `B Standard 5-string` |
| `ERG` | `7` | `ERG B Standard 7` | `ERG F# Standard 8` | `ERG B Standard 5` |
| empty | empty | `B Standard` | `F# Standard 8Str` | `B Standard 5Str` |

A suffix without a `7` keeps the default mark on eight-string guitars and basses, as in the last row.

Tuning names are in English. Tuning filters and sorting still use the six-string tuning.

## Chords: `[chords]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `wide_misses` | `2` | How many notes of a seven- or eight-note chord you can miss, on any string, and still match the chord (0-2). Five- and six-note chords allow one miss. Two- to four-note chords allow none, as in the stock game. | Live | yes | no |
| `diagrams7` | `1` | Chord diagrams with rows for the seventh and eighth strings. `1` uses the recipes in the `chord-diagrams` folder to change the game's diagram movies in memory. This works on the September 2022 build only. A movie that does not match keeps the stock diagrams. `0` turns it off, and the mod does not read the folder. | Restart | no | yes |

## Headstock files: `[assets]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `headstock7` | `1` | Shows the mod's headstocks, which are the seven- and eight-peg guitar headstocks and the five- and six-string bass headstocks. `0` keeps the stock headstock on every instrument. | Restart | yes | no |
| `selfbuild` | `1` | Builds missing headstock models from your own game into `dir` at first use. When the package changes, it also rebuilds the default models. A build replaces generated files whose contents differ. If a build fails, the game keeps the stock headstock. `0` turns off all headstock builds. Needs the four `rsxr_headstock*.bin` files next to the DLL ([install guide](INSTALL.md#3-headstock-built-on-first-use)). | Restart | no | yes |
| `headstock7_file` | `rsxr_inline7.nif` | Secondary model for the right-handed in-line headstock, shown on seven-string guitars. A relative name is taken from `dir`. An empty value, `none`, `off` or `0` disables this secondary model. It does not turn off the builder. | Restart | yes | no |
| `headstock7_file_lh` | `rsxr_inline7_lh.nif` | Secondary model for the left-handed in-line headstock, shown on seven-string guitars. Same rules as `headstock7_file`. | Restart | no | no |
| `headstock8_file` | `rsxr_inline8.nif` | Primary model for the right-handed in-line headstock, with eight pegs. A relative name is taken from `dir`. An empty value, `none`, `off` or `0` uses only the `dir` + `suffix` rule and turns off the guitar headstock builder for this hand. | Restart | yes | no |
| `headstock8_file_lh` | `rsxr_inline8_lh.nif` | Primary model for the left-handed in-line headstock, with eight pegs. Same rules as `headstock8_file`. | Restart | no | no |
| `headstock7_wood` | `0.6` | Brightness of the model's wood texture (`1` = as built). Valid values are 0.05-1. Any other value uses `0.6`. | Restart | yes | no |
| `dir` | `rsxr\headstock\` | Folder for the built models and for edited models named `<dir>\<stock base><suffix>.nif`. A relative path is taken from the DLL's folder, which is the game folder. | Restart | yes | no |
| `suffix` | `_7inline` | The file-name suffix of edited models. A value containing `\`, `/`, `:` or `.` falls back to `_7inline`. | Restart | yes | no |

See [INSTALL](INSTALL.md#3-headstock-built-on-first-use) for the headstock builder and its log
lines. Give your own models and textures other file names, and point the model keys at them, so that a rebuild
does not replace them. Never share generated `.nif` files.

## Plain packages: `[psarc]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `plain` | `1` | Loads plain (unencrypted) PSARC charts with any CDLC enabler for the supported game build. You still need the enabler, but not a special plain PSARC build of it. If the enabler already handles plain packages, RS Extended Range leaves that to the enabler. `0` turns off RS Extended Range's plain-package loader. | Restart | no | no |

## Window: `[window]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `console_quickedit_off` | `1` | Turns off QuickEdit in the CDLC enabler's console, so a click there can no longer freeze the game on the loading screen or during play. Choosing Edit > Mark in the console menu can still freeze the game. Press Enter in the console to end the selection. `0` leaves QuickEdit unchanged. | Restart | no | no |
| `screen` | `0` | Puts the game window on monitor N. The count starts with the primary monitor, then the others follow in Windows order. This order isn't the same as the Windows display numbers. `0`, or a monitor that does not exist, keeps the stock placement. The overlay allows 0-16. | Restart | yes | no |

## Fixes: `[fixes]`

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `menu_retry` | `1` | Stops a menu from staying on the white loading screen. This can happen when the game asks for the menu file before it has finished indexing its files at startup. The mod sends the request again as soon as that file is indexed. If the mod never sees that, it sends the request every 5 seconds for up to 30 seconds. `0` turns this fix off. If another mod already changed these loading functions, RS Extended Range leaves them alone. | Restart | no | no |

## Neck: `[neck]`

> [!WARNING]
> These are advanced settings. Keep them at their defaults unless you are adjusting the visuals.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `string7_line` | `1` | Shows the extra string line. `0` hides it. | Restart | yes | no |
| `spacing_bass5` | `0` | Distance between the strings of a five-string bass on the neck and the note highway. `0` keeps the normal bass spacing (2.68). `2.01` makes the neck as wide as a four-string bass, and `1.608` is the guitar spacing. Valid values are 0.1-4. A value below 0.1 counts as `0`. The headstock gets narrower with the neck. Applies the next time the bass is built, which happens with a song for another instrument or string count, or after a restart. | Next build | yes | no |
| `spacing_bass6` | `0` | The same for a six-string bass. At the normal bass spacing, its neck is two thirds wider than a guitar's. `1.608` (the guitar spacing) makes it as wide as a four-string bass. | Next build | yes | no |
| `spacing_guitar7` | `0` | The same for a seven-string guitar. `0` keeps the guitar spacing (1.608). `1.34` makes the neck as wide as a six-string guitar. | Next build | yes | no |
| `spacing_guitar8` | `0` | The same for an eight-string guitar. `0` keeps the guitar spacing (1.608). `1.15` makes the neck as wide as a six-string guitar. | Next build | yes | no |

Extended bass has no setting. It turns on only for a bass arrangement whose native chart specifies five or six bass
strings. See the [native format](NATIVE-FORMAT.md). The game then builds the bass with five or six strings, and it
gets its own headstock, `rsxr_bass5*.nif` or `rsxr_bass6*.nif` in the headstock folder. Four-string bass and guitar charts are not affected.

## Debug: `[debug]`

> [!WARNING]
> These settings are for finding problems only. Leave them at their defaults for normal play.

| Key | Default | Effect | Apply | F9 | Exp. |
|---|---|---|---|---|---|
| `diagnostics` | `0` | `1` turns on extra diagnostic logging for bug reports. Some parts only take effect after a restart. | Live (some parts: Restart) | yes | yes |
| `song_end_log` | `1` | Logs how each song ended (for bug reports). | Live | yes | no |
| `log_redact` | `1` | `rsxr.log` hides your Windows user name (`<user>`), your user folder (`%USERPROFILE%`) and your computer name (`<pc>`). `0` writes them as they are. Use this only for your own debugging, and never post such a log. | Restart | no | no |

Developer and test keys for test driving, bisection and extra traces aren't part of the release template.
[DEVELOPMENT.md](../DEVELOPMENT.md#developer-and-test-settings) lists them.

## Removed settings

Version 1.0 removed these switches, and the settings that only changed internal values. An `rsxr.ini` that still
has them loads normally. The mod ignores each one, and the log says `ignored, removed in 1.0`.

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
- `[color] ramp_swap`, `allow_inexact` (the gem color copy always runs, without DXT ramps)
- `[chords] frame_scale`, `frame_axis` (the chord frame follows the lane positions)
- `[chords] native_chordloops` (chord bends, sustain techniques and chord frames always cover strings 7 and 8)
- `[debug] head_stretch`, `head_stretch_mode` (a headstock test)
