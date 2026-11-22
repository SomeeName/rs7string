# Install RS Extended Range

**Install, check, update and remove RS Extended Range on Windows.**

[README](../../README.md) · **Install** · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

In this guide, **the game folder** is the folder that contains `Rocksmith2014.exe`.

| Term | Meaning |
|---|---|
| CDLC | Custom songs made by players, as `.psarc` packages in the game's `dlc` folder. |
| CDLC enabler | A separate mod that lets the game load CDLC, for example [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix) (installed as `D3DX9_42.dll`). |
| Plain PSARC | An unencrypted song package, usually named `*_p.psarc`. Charts for RS Extended Range use this format. |
| F9 overlay | RS Extended Range's in-game menu for status and settings. Press **F9** in the game to open or close it. |
| RSMods, RS_ASIO | Optional, separate mods. [RSMods](https://github.com/Lovrom8/RSMods) adds game tweaks. RS_ASIO lets the game use ASIO audio interfaces. |

> [!TIP]
> Close the game, back up your files, copy the mod files into the game folder and put a chart into
> `dlc`. Then start the game and press **F9** to check that the mod runs. The rest of this page explains each step
> and what to do if something goes wrong.

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
| Rocksmith 2014 Remastered (Steam) | **September 2022 build** only. Learn & Play is a different program and is not supported. The mod checks the build at startup and does not patch other builds. On another build, `rsxr.log` says `unsupported game build ... nothing patched`. |
| A CDLC enabler for that build | Install it following its own instructions. You still need it, because without it the game doesn't accept custom songs. |
| A 7- or 8-string guitar, or a 5- or 6-string bass | You also need a working Rocksmith audio input. RS_ASIO is optional. |
| Charts for those instruments | Plain (unencrypted) PSARC packages (`*_rsxr_p.psarc`) made for RS Extended Range. See the [charting guide](CHARTING.md). |

> [!NOTE]
> RS Extended Range loads plain PSARC packages itself (`[psarc] plain=1`, the default). You do **not** need
> the special plain-PSARC enabler build from
> [RSCDLCEnabler-TooManyCoresFix pull #1](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix/pull/1).
> If you have it, it still works, and RS Extended Range leaves plain-package loading to it. RS Extended Range was
> tested with RSCDLCEnabler-TooManyCoresFix, both with and without that change.

## 1. Close the game and back up

> [!WARNING]
> Never change files in the game folder while Rocksmith is running.

1. Close Rocksmith completely.
2. In Steam, open Rocksmith 2014's **Properties > Installed Files > Browse**. This opens the game folder.
3. If the game folder already has `dinput8.dll`, `rsxr.ini` or a `chord-diagrams` folder, move
   them to a backup folder outside the game folder.
4. If that `dinput8.dll` belongs to another mod, check that the two can work together before you
   replace it. See [Compatibility](#compatibility).
5. Install a CDLC enabler following its own instructions. Back up any file it replaces.

## 2. Copy the mod files

1. Download the zip from the [latest release](https://github.com/SomeeName/rs-extended-range/releases/latest).
   Extract it to a folder **outside** the game folder. The mod files are in its `bin\Release` folder.
2. Copy these files into the game folder. Don't put them in `dlc`.

   | File | What it is |
   |---|---|
   | `dinput8.dll` | The mod. |
   | `rsxr.ini` | The settings file, with every key at its default. |
   | `rsxr_headstock.bin` | Data for the headstock builder (right-handed, plus the shared textures). |
   | `rsxr_headstock_lh.bin` | Data for the headstock builder (left-handed). |
   | `rsxr_headstock_bass.bin` | Data for the headstock builder (five- and six-string bass). |
   | `rsxr_headstock_bass_lh.bin` | Data for the headstock builder (bass, left-handed). |
   | `rsxr_headstock.LICENSE.md` | The license of the headstock data. |
   | `chord-diagrams` folder | Recipes for the seven- and eight-row chord diagrams. Copy the whole folder. |

> [!TIP]
> The `chord-diagrams` folder holds `commonlib.rsxrda`, `contentpanel.rsxrda`, `ge_game.rsxrda`,
> `LICENSE` and `README.md`. When `[chords] diagrams7=1` is set, which is the default, the mod uses these recipes to
> change the game's chord-diagram movies in memory. If a movie doesn't match the September 2022 build, the mod leaves
> it unchanged.

## 3. Headstock (built on first use)

The release doesn't include the seven- and eight-string headstock models or the five- and six-string bass
headstocks, because the models are made from the game's own files. The headstock builder creates them from your own
game install.

- It runs once per hand, the first time the game loads a guitar. The bass models are built the first time the game
  loads a five- or six-string bass. This can happen before you pick a song, and the first load can take a moment.
- It writes the model into the `rsxr\headstock` folder in the game folder. Later starts reuse it.
  When the headstock package changes, the builder rebuilds the default models and replaces generated files whose
  contents differ.
- It reads the game's files through the game's own loader and uses no keys.
- It needs `rsxr_headstock.bin` next to `dinput8.dll`. The left-handed models also need
  `rsxr_headstock_lh.bin`. The bass models need `rsxr_headstock_bass.bin`, and the left-handed
  bass models need `rsxr_headstock_bass_lh.bin`.
- If the build fails, the game uses the stock headstock.

To keep the stock headstock on every instrument, set `headstock7=0` under `[assets]` in `rsxr.ini`.

<details markdown="1">
<summary><b>Log lines of the headstock builder</b></summary>

Text in square brackets is optional. Paths and details vary.

| Log line | Meaning |
|---|---|
| `hsbuild: built in <dir>: <detail>` | The model was built. |
| `hsbuild: cached in <dir>: <detail>` | The model is already there from an earlier start. |
| `assetload: self-built [left-handed ]<n>-peg model in use: <path>` | The model is shown in game. |
| `hsbuild: FAILED in <dir>: <reason>` | The build failed. The game uses the stock headstock. |
| `hsbuild: missing/unreadable <bin> next to DLL` | A `.bin` file is missing. |

`hsbuild:` lines for the other models have `left-handed `, `bass ` or `left-handed bass ` before the result.

</details>

<details markdown="1">
<summary><b>Advanced: use your own headstock model</b></summary>

Use this only if you want to supply your own model instead of the built one.

1. Close Rocksmith.
2. Copy your model and its textures into `rsxr\headstock`. Keep the textures beside the models.
   Use file names that differ from the generated ones, so that a rebuild does not replace them.
3. In `rsxr.ini`, under `[assets]`, set `headstock8_file` for the right-handed primary in-line model and
   `headstock8_file_lh` for the left-handed one. These seat eight pegs. Set `headstock7_file` and `headstock7_file_lh`
   for the secondary seven-peg models shown on seven-string guitars. Use absolute paths, or names relative to
   `[assets] dir`.
4. Save the file as `rsxr.ini`, not `rsxr.ini.txt`.

Rules:

- Alternate models for other stock headstocks are named `<dir>\<stock base><suffix>.nif`.
- Copying `rsxr_peg.nif` alone does not replace the peg. The game uses the stock peg unless you supply a file
  named as above. The headstock builder writes `rsxr_peg.nif`, but the game does not
  necessarily load it.
- If a standard primary model file is missing, `selfbuild=1` (the default) builds the standard in-line models.
  A model file you named yourself is never built. If `selfbuild` is off, the build fails, or your own file is
  missing, the mod looks for an edited file named as above. If there is none, it keeps the stock model.
- An empty value, `none`, `off` or `0` for `headstock8_file` or `headstock8_file_lh` turns off the guitar
  headstock builder for that hand. The seven-peg keys only disable the secondary models. `selfbuild=0` turns
  off all headstock builds, including bass.
- The release does not include a separate authoring tool. The headstock builder itself needs no Python,
  uv or Blender.

</details>

## 4. Add a test chart and check the install

Charts must use the native format. If a chart was made for an earlier version, convert it with the
[chart converter](CHARTING.md#charts-made-for-earlier-versions) from the release zip before you play it.

1. Close Rocksmith. Copy a known-good plain PSARC chart (`*_rsxr_p.psarc`) for your instrument into the game
   folder's `dlc` folder. Back up any package with the same name first. If you have none, make one with
   [DotCharter](https://github.com/SomeeName/DotCharter). The [charting guide](CHARTING.md) explains how.
2. Start Rocksmith through Steam. Press **F9** to open the RS Extended Range overlay, then open **Advanced** and
   check its **Status** and **Log** tabs for errors.
3. Choose the extended arrangement. You should see a tuning name with the string count, such as
   `B Standard 7Str`, and a tuner step for each extra string. Tune your real instrument.
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

- The log is `rsxr.log`, next to `dinput8.dll`. It records the startup patch results. It also records whether
  a loaded song uses the extra strings, and how many. It replaces your Windows user name, user folder and computer
  name with `<user>`, `%USERPROFILE%` and `<pc>`, so you can share it. Still, check it before you post it.
- The settings are in `rsxr.ini`, next to `dinput8.dll`. Any key that is missing from the file uses its default.
  [SETTINGS](SETTINGS.md) lists every key and whether it takes effect live, on restart, or at the next
  song.

## Update

1. Close Rocksmith.
2. Replace `dinput8.dll`, the four `.bin` files, `rsxr_headstock.LICENSE.md` and the
   `chord-diagrams` folder with the new release files. Keep your own `rsxr.ini` if you changed it.
3. Leave the `rsxr\headstock` folder as it is. If the new version ships a changed headstock, the mod rebuilds the
   models it built from your game files by itself. It keeps your own models.

### Updating from 1.0.0-beta.4 or earlier

The mod now uses only the `rsxr` file names and no longer reads the old files.

1. Close Rocksmith. To keep your settings, rename `rs7string.ini` to `rsxr.ini` and keep it instead of the
   settings file from the zip. Copy the new release files as in [step 2](#2-copy-the-mod-files). Settings that no
   longer exist are ignored, and `rsxr.log` names each one.
2. Move any models of your own from `rs7string\headstock` to `rsxr\headstock`. If you set `dir` under `[assets]`
   to the old folder, change it to `rsxr\headstock\` or remove the line to use the default.
3. Delete `rs7string.log`, the `rs7string_headstock*` files, the old `rs7string` folder and the `*.rs7da` files in
   `chord-diagrams`.
4. The mod builds the headstock again on the first start, in `rsxr\headstock`.
5. Convert your extended-range charts once. Charts made for 1.0.0-beta.4 or earlier are in an older format that the
   mod no longer reads. Drag your `dlc` folder, or single `_p.psarc` files, onto `tools\rsxr-convert.exe` from the
   zip.
   - For every old chart, the converter writes `<name>_rsxr_p.psarc` next to it. It moves the old file to a
     subfolder `old-format` as `<name>_p.disabled.psarc`, so the game no longer loads it.
   - The converter leaves official DLC, ordinary customs and charts that are already converted alone. It deletes
     nothing.
   - A window lists what the converter did. When everything plays, delete the `old-format` folders.
   - To go back to an older version of the mod, rename an old file to `<name>_p.psarc`, move it out of
     `old-format` and delete the `<name>_rsxr_p.psarc` beside it.

## Compatibility

| Component | Works with RS Extended Range? |
|---|---|
| CDLC enabler | Required. Any enabler for the September 2022 build. |
| RSMods | Yes, with or without it (verified). It uses `xinput1_3.dll`. Check its keybindings if F9 is already taken. RSMods custom note colors can override the extra strings' gem colors. |
| RS_ASIO | Yes, with or without it (verified). |
| Another mod using `dinput8.dll` | Only with a loading setup that lets both mods load. Back up the other mod's file before you install. |
| macOS | Not supported. |
| Wine/Proton, Steam Deck | Not tested. |

## Troubleshooting

| Problem | What to check |
|---|---|
| No overlay and no log | `dinput8.dll` and `rsxr.ini` must be in the game folder, beside `Rocksmith2014.exe`. Check the DLL file name and the game build. Make sure that another mod has not replaced `dinput8.dll`. |
| The log reports patch or site failures | Check that the game is the September 2022 build. Keep the log for an issue report. |
| F9 does nothing, but the log exists | Check `enabled=1` and `hotkey` under `[overlay]`, and the RSMods keybindings. After you change the file, restart the game. |
| The chart is missing or fails to load | The chart must be a PC plain PSARC in `dlc`, with a CDLC enabler. Plain PSARC loading comes from RS Extended Range (`[psarc] plain=1`, the default) or from an enabler that has it. The log should have a `plain psarc: provided by RSXR` or `plain psarc: provided by the CDLC enabler` line. An enabler with its own plain PSARC support may log `Plain PSARC: pattern not found` when RS Extended Range patched the game first. That is expected. Ask the chart author to check the `ExtendedRange` marker and positional tuning entries for your instrument in the [native format](NATIVE-FORMAT.md). |
| The chart loads with the wrong number of strings | The chart must be made for your extended instrument. Renaming an ordinary chart does not convert it. After the song loads, check the string count in the overlay or the log. |
| Stock headstock, or no headstock build | Check that the `.bin` files are next to `dinput8.dll`, and that `headstock7=1` and `selfbuild` is not `0` under `[assets]`. Check the `hsbuild:` lines in the log. For your own model, check `headstock8_file`, and `headstock7_file` for the seven-string model. Also check that the `.dds` textures sit beside the model. Restart after changing asset settings. To retry a build, close the game and rename `rsxr\headstock`. |
| Wrong gem color on an extra string | Check the `[color]` settings. RSMods custom note colors can override the gem colors. |
| A song stops after about seven seconds with orange speaker rings | This is a game or audio-device issue. Check your normal Rocksmith audio setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| No sound or poor detection on all strings | First check your normal Rocksmith input setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| Arrow keys do not select tuner strings | Enter the precision view with Space or by playing a note. |
| A setting seems ignored | Some changes need a restart or a new tuner session. See [SETTINGS](SETTINGS.md). Edits to the file usually need a restart. |

> [!TIP]
> When you report a bug, include the mod version, game build, chart and arrangement, tuning, changed
> INI settings, other mods, the steps to reproduce, and the relevant lines from `rsxr.log`.
> Remove personal file paths and don't attach game files. Report it in the
> [issue tracker](https://github.com/SomeeName/rs-extended-range/issues).

## Known limitations

| Area | Limitation |
|---|---|
| Co-op with an extended bass | When a guitar player is in the song, the bass's low B notes and string are grey, but its string glow, fret wires, scale markers and chord diagram dots use the guitar's B string color. |
| Co-op arrangement list | When "Other arrangements" has more than eight arrangements and you scroll, the song length in the header shows an extra digit ("1:001" for "1:00") until you leave the menu. |
| Riff Repeater | On six-string bass, a note's tail is rarely drawn in another string's color. Pause and resume to fix it. |
| Game version | Only the September 2022 build. Learn & Play is not supported. |
| Chord diagrams | Some generic chords from the game's own chord database still show six rows. Highway chords always show the extra strings. |
| Chord matching | By default, you can miss up to two notes of a seven- or eight-note chord, including the lowest strings. Use `[chords] wide_misses=0` for strict checks. |
| Tuner | After a seven- or eight-string song, the menu tuner keeps the extra pegs until the guitar returns to six strings. That happens with the next six-string song, or in Session Mode, Guitarcade, Lessons or the technique hub. Arrow keys work only in the precision view. |
| Tuning names | Tuning names are in English only. Tuning filters and sorting still use the six-string tuning. Not every alternate tuning has its own name for the extended instruments. |
| Headstock | The in-line model is shown with the game's 6-in-line headstock option (German UI: "6 in Reihe"). With the 3+3 option, a seven- or eight-string guitar shows the same in-line model and pegs. Six-string guitars keep the stock 3+3 headstock. With the reversed string order, headstock strings 1-6 fade out before the posts do. |
| Colors | Distant gems on the extra strings can blend neighboring colors on some textures. |

> [!NOTE]
> A few things still need a final release check with a real guitar. These are bends, slides, harmonics and chord
> techniques on the extra strings, the bend reset after seeking or restarting, and tuner thresholds in low tunings.

## Uninstall

1. Close Rocksmith.
2. From the game folder, delete `dinput8.dll`, `rsxr.ini`, the four `rsxr_headstock*.bin`
   files, `rsxr_headstock.LICENSE.md` and the `chord-diagrams` folder.
   Delete only the files you installed for this mod.
3. Restore any files you backed up in [step 1](#1-close-the-game-and-back-up).
4. Move extended charts out of `dlc`, because without RS Extended Range the game doesn't support their extra
   strings.
5. If you want, delete `rsxr.log` and the `rsxr` folder with the built headstock.

Keep or remove the CDLC enabler, RSMods and RS_ASIO following their own instructions. They are
separate projects.
