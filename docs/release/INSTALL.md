# Install rs7string

**Install, check, update and remove rs7string on Windows.**

[README](../../README.md) · **Install** · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

In this guide, **the game folder** is the folder that contains `Rocksmith2014.exe`.

| Term | Meaning |
|---|---|
| CDLC | Custom songs made by players, as `.psarc` packages in the game's `dlc` folder. |
| CDLC enabler | A separate mod that lets the game load CDLC, for example [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix) (installed as `D3DX9_42.dll`). |
| Plain PSARC | An unencrypted song package, usually named `*_p.psarc`. Charts for rs7string use this format. |
| F9 overlay | rs7string's in-game menu for status and settings. Press **F9** in the game to open or close it. |
| RSMods, RS_ASIO | Optional, separate mods: [RSMods](https://github.com/Lovrom8/RSMods) adds game tweaks, RS_ASIO lets the game use ASIO audio interfaces. |

> [!TIP]
> **In short:** close the game, back up, copy the mod files into the game folder, put a chart into `dlc`, start the
> game and press **F9** to check. The rest of this page explains each step and what to do if something is off.

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [Requirements](#requirements)
- [1. Close the game and back up](#1-close-the-game-and-back-up)
- [2. Copy the mod files](#2-copy-the-mod-files)
- [3. Headstock (built on first use)](#3-headstock-built-on-first-use)
- [4. Add a test chart and check the install](#4-add-a-test-chart-and-check-the-install)
- [Logs and settings](#logs-and-settings)
- [Update](#update)
- [Compatibility](#compatibility)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Uninstall](#uninstall)

</details>

## Requirements

| You need | Notes |
|---|---|
| Rocksmith 2014 Remastered (Steam) | **September 2022 build** only. Learn & Play is a different program and is not supported. The mod checks the build at startup and does not patch other builds; on another build `rs7string.log` says `unsupported game build ... nothing patched`. |
| A CDLC enabler for that build | Install it following its own instructions. It is still needed so the game accepts custom songs. |
| A 7- or 8-string guitar, or a 5- or 6-string bass | With a working Rocksmith audio input. RS_ASIO is optional. |
| Charts for those instruments | Plain (unencrypted) PSARC packages (`*_p.psarc`) made for rs7string. See the [charting guide](CHARTING.md). |

> [!NOTE]
> rs7string loads plain PSARC packages itself (`[psarc] plain=1`, the default). You do **not** need
> the special plain-PSARC enabler build from
> [RSCDLCEnabler-TooManyCoresFix pull #1](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix/pull/1).
> It still works if you have it: rs7string then leaves plain-package loading to it. Tested with
> RSCDLCEnabler-TooManyCoresFix with and without that change.

## 1. Close the game and back up

> [!WARNING]
> Never change files in the game folder while Rocksmith is running.

1. Close Rocksmith completely.
2. In Steam, open Rocksmith 2014's **Properties > Installed Files > Browse**. This opens the game folder.
3. If the game folder already has `dinput8.dll`, `rs7string.ini` or a `chord-diagrams` folder, move
   them to a backup folder outside the game folder.
4. If that `dinput8.dll` belongs to another mod, check that the two can work together before you
   replace it (see [Compatibility](#compatibility)).
5. Install a CDLC enabler following its own instructions. Back up any file it replaces.

## 2. Copy the mod files

1. Download the zip from the [latest release](https://github.com/SomeeName/rs7string/releases/latest) and extract it to a folder **outside** the game folder. The
   files are in its `bin\Release` folder.
2. Copy these into the game folder (not into `dlc`):

   | File | What it is |
   |---|---|
   | `dinput8.dll` | The mod. |
   | `rs7string.ini` | Settings, every key at its default. |
   | `rs7string_headstock.bin` | Data for the headstock builder (right-handed, also shared textures). |
   | `rs7string_headstock_lh.bin` | Data for the headstock builder (left-handed). |
   | `rs7string_headstock_bass.bin` | Data for the headstock builder (five- and six-string bass). |
   | `rs7string_headstock_bass_lh.bin` | Data for the headstock builder (bass, left-handed). |
   | `rs7string_headstock.LICENSE.md` | Licence of the headstock data. |
   | `chord-diagrams` folder | Recipes for the seven-row chord diagrams. Copy the whole folder. |

> [!TIP]
> The `chord-diagrams` folder holds `commonlib.rs7da`, `contentpanel.rs7da`, `ge_game.rs7da`,
> `LICENSE` and `README.md`. With `[chords] diagrams7=1` (the default) the mod uses these recipes to patch the
> game's chord-diagram movies in memory. A movie that does not match the September 2022 build is left
> unchanged.

## 3. Headstock (built on first use)

The seven- and eight-string headstock models and the five- and six-string bass headstocks are **not**
shipped. The headstock builder creates them from your own game install:

- It runs once per hand, at the first guitar load (the bass models at the first five- or six-string bass
  load). This can happen before you pick a song, and the
  first load can take a moment.
- It writes the model into the `rs7string\headstock` folder in the game folder. Later starts reuse it.
  A changed headstock package rebuilds the default models and replaces generated files whose contents differ.
- It reads the game's files through the game's own loader and uses no keys.
- It needs `rs7string_headstock.bin` next to `dinput8.dll`. The left-handed models also need
  `rs7string_headstock_lh.bin`, the bass models `rs7string_headstock_bass.bin` (left-handed:
  `rs7string_headstock_bass_lh.bin`).
- If the build fails, the stock headstock is used.

To keep the stock headstock on every instrument, set `headstock7=0` under `[assets]` in `rs7string.ini`.

<details markdown="1">
<summary><b>Log lines of the headstock builder</b></summary>

Text in square brackets is optional; paths and details vary.

| Log line | Meaning |
|---|---|
| `hsbuild: built in <dir>: <detail>` | Model built. |
| `hsbuild: cached in <dir>: <detail>` | Model already there from an earlier start. |
| `assetload: self-built [left-handed ]<n>-peg model in use: <path>` | The model is shown in game. |
| `hsbuild: FAILED in <dir>: <reason>` | Build failed; the stock headstock is used. |
| `hsbuild: missing/unreadable <bin> next to DLL` | A `.bin` file is missing. |

`hsbuild:` lines for the other models have `left-handed `, `bass ` or `left-handed bass ` before the result.

</details>

<details markdown="1">
<summary><b>Advanced: use your own headstock model</b></summary>

Use this only if you want to supply your own model instead of the built one.

1. Close Rocksmith.
2. Copy your model and its textures into `rs7string\headstock`. Keep the textures beside the models.
   Use file names other than the generated ones so a rebuild does not replace them.
3. In `rs7string.ini`, under `[assets]`, set `headstock8_file` (right-handed primary in-line model) and
   `headstock8_file_lh` (left-handed). These seat eight pegs. Set `headstock7_file` and `headstock7_file_lh`
   for the secondary seven-peg models shown on seven-string guitars. Use absolute paths, or names relative to `[assets] dir`.
4. Save the file as `rs7string.ini`, not `rs7string.ini.txt`.

Rules:

- Alternate models for other stock headstocks are named `<dir>\<stock base><suffix>.nif`.
- Copying `rs7_peg.nif` alone does not replace the peg. The stock peg is used unless a file with the
  naming above is supplied. The headstock builder writes `rs7_peg.nif`, but the game does not
  necessarily load it.
- If a primary model file is missing, `selfbuild=1` (the default) builds the standard in-line models. If
  `selfbuild` is off or the build fails, the mod tries an edited file with the naming above, and
  otherwise keeps the stock model.
- An empty value, `none`, `off` or `0` for `headstock8_file` or `headstock8_file_lh` turns off the guitar
  headstock builder for that hand. The seven-peg keys only disable the secondary models. `selfbuild=0` turns
  off all headstock builds, including bass.
- A separate authoring tool is not part of the release. The headstock builder itself needs no Python,
  uv or Blender.

</details>

## 4. Add a test chart and check the install

1. Close Rocksmith. Copy a known-good plain PSARC chart (`*_p.psarc`) for your instrument into the game
   folder's `dlc` folder. Back up any package with the same name first. If you have none, make one with
   [DotCharter](https://github.com/SomeeName/DotCharter) (see the [charting guide](CHARTING.md)).
2. Start Rocksmith through Steam. Press **F9** to open the rs7string overlay, then open **Advanced** and
   check its **Status** and **Log** tabs for errors.
3. Choose the extended arrangement. Expect a tuning name with the string count, such as
   `B Standard 7Str`, and a tuner step for each extra string. Tune with your real instrument.
4. Play the chart. Check that you see the right number of strings and lanes and the matching headstock,
   and that notes on the extra strings are detected. The table below shows what to expect.
5. Play an ordinary six-string guitar or four-string bass arrangement as a control. It should look and play as
   before.

| Instrument | Extra strings in standard tuning | Headstock |
|---|---|---|
| 7-string guitar | low B | seven pegs |
| 8-string guitar | low B, low F# | eight pegs |
| 5-string bass | low B | five pegs, in line |
| 6-string bass | low B, high C | six pegs, in line |

## Logs and settings

- **Log:** `rs7string.log`, next to `dinput8.dll`. It records the startup patch results and whether
  a loaded song uses the extra strings and how many. It hides your Windows user name, user folder and computer name
  (shown as `<user>`, `%USERPROFILE%` and `<pc>`), so you can share it; check it before you post it anyway.
- **Settings:** `rs7string.ini`, next to `dinput8.dll`. Missing keys use the defaults.
  [SETTINGS](SETTINGS.md) lists every key and whether it takes effect live, on restart, or at the next
  song.

## Update

1. Close Rocksmith.
2. Replace `dinput8.dll`, the four `.bin` files, `rs7string_headstock.LICENSE.md` and the
   `chord-diagrams` folder with the new release files. Keep your own `rs7string.ini` if you changed it.
3. Rename the `rs7string\headstock` folder to a backup name, so the headstock is rebuilt with the new
   version on the next start.

## Compatibility

| Component | Works with rs7string? |
|---|---|
| CDLC enabler | Required. Any enabler for the September 2022 build. |
| RSMods | Yes, with or without it (verified). It uses `xinput1_3.dll`. Check its keybindings if F9 is already taken. RSMods custom note colours can override the extra strings' gem colours. |
| RS_ASIO | Yes, with or without it (verified). |
| Another mod using `dinput8.dll` | Needs a compatible loading setup. Back it up before installing. |
| macOS | Not supported. |
| Wine/Proton, Steam Deck | Not tested. |

## Troubleshooting

| Problem | What to check |
|---|---|
| No overlay and no log | `dinput8.dll` and `rs7string.ini` must be in the game folder, beside `Rocksmith2014.exe`. Check the DLL file name and the game build. Make sure another mod has not replaced `dinput8.dll`. |
| The log reports patch or site failures | Check that the game is the September 2022 build. Keep the log for an issue report. |
| F9 does nothing, but the log exists | Check `enabled=1` and `hotkey` under `[overlay]`, and the RSMods keybindings. File changes need a restart. |
| The chart is missing or fails to load | The chart must be a PC plain PSARC in `dlc`, with a CDLC enabler and `[psarc] plain=1`. The log should have a `plain psarc: provided by rs7string` or `plain psarc: provided by the CDLC enabler` line. An enabler with its own plain PSARC support may log `Plain PSARC: pattern not found` when rs7string patched first; that is expected. Ask the chart author to check the tuning entries and `Rs7String` keys for your instrument in the [charting guide](CHARTING.md). |
| The chart loads with the wrong number of strings | The chart must be made for your extended instrument. Renaming an ordinary chart does not convert it. Check the string count in the overlay or the log after the song loads. |
| Stock headstock, or no headstock build | Check that the `.bin` files are next to `dinput8.dll`, that `headstock7=1` and `selfbuild` is not `0` under `[assets]`, and the `hsbuild:` lines in the log. For your own model, check `headstock7_file` and that the `.dds` textures sit beside the model. Restart after changing asset settings. To retry a build, close the game and rename `rs7string\headstock`. |
| Wrong gem colour on an extra string | Check the `[color]` settings. RSMods custom note colours can override the gem colours. |
| A song stops after about seven seconds with orange speaker rings | A game or audio-device issue. Check your normal Rocksmith audio setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| No sound or poor detection on all strings | First check your normal Rocksmith input setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| Arrow keys do not select tuner strings | Enter the precision view with Space or by playing a note. |
| A setting seems ignored | Some changes need a restart or a new tuner session; see [SETTINGS](SETTINGS.md). File edits generally need a restart. |

> [!TIP]
> **Reporting a bug:** include the mod version, game build, chart and arrangement, tuning, changed
> INI settings, other mods, the steps to reproduce, and the relevant lines from `rs7string.log`.
> Remove personal file paths and do not attach game files. Report it in the
> [issue tracker](https://github.com/SomeeName/rs7string/issues).

## Known limitations

| Area | Limitation |
|---|---|
| Co-op with an extended bass | With a guitar player in the song the bass's low B notes and string are grey, but its string glow, fret wires, scale markers and chord diagram dots use the guitar's B string colour. |
| Co-op arrangement list | "Other arrangements" with more than eight arrangements: after scrolling, the song length in the header shows an extra digit ("1:001" for "1:00") until you leave the menu. |
| Riff Repeater | Six-string bass: rarely, a note's tail is drawn in another string's colour. Pausing and resuming fixes it. |
| Game version | Only the September 2022 build. Learn & Play is not supported. |
| Chord diagrams | Some generic chords from the game's own chord database still show six rows. Highway chords always show the extra strings. |
| Chord matching | By default a seven- or eight-note chord may miss up to two notes, including the lowest strings. Use `[chords] wide_misses=0` for strict checks. |
| Tuner | After a seven- or eight-string song, the menu tuner keeps the extra pegs until the guitar returns to six strings (next six-string song, or Session Mode, Guitarcade, Lessons or the technique hub). Arrow keys work only in the precision view. |
| Tuning names | English only. Tuning filters and sorting still use the six-string tuning. Not every alternate tuning has its own name for the extended instruments. |
| Headstock | The in-line model is shown with the game's 6-in-line headstock option (German UI: "6 in Reihe"). With the 3+3 option a seven- or eight-string guitar shows the same in-line model and pegs; six-string guitars keep the stock 3+3 headstock. With the reversed string order, headstock strings 1-6 fade out before the posts do. |
| Colours | Distant gems on the extra strings can blend neighbouring colours on some textures. |

> [!NOTE]
> Real-guitar bends, slides, harmonics and chord techniques on the extra strings still need a final
> release check, as do bend reset after seeking or restarting and tuner thresholds in low tunings.

## Uninstall

1. Close Rocksmith.
2. From the game folder, delete `dinput8.dll`, `rs7string.ini`, the four `rs7string_headstock*.bin`
   files, `rs7string_headstock.LICENSE.md` and the `chord-diagrams` folder.
   Delete only files you installed for this mod.
3. Restore any files you backed up in [step 1](#1-close-the-game-and-back-up).
4. Move extended charts (seven- and eight-string guitar, five- and six-string bass) out of `dlc`. The game
   without rs7string does not support their extra strings.
5. Optional: delete `rs7string.log` and the `rs7string` folder with the built headstock.

Keep or remove the CDLC enabler, RSMods and RS_ASIO following their own instructions. They are
separate projects.
