# Install rs7string

**Install, check, update and remove rs7string 1.0 on Windows.**

[README](../../README.md) · **Install** · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

In this guide, **the game folder** is the folder that contains `Rocksmith2014.exe`.

## Contents

- [Requirements](#requirements)
- [1. Close the game and back up](#1-close-the-game-and-back-up)
- [2. Copy the mod files](#2-copy-the-mod-files)
- [3. Headstock (built on first start)](#3-headstock-built-on-first-start)
- [4. Add a test chart and check the install](#4-add-a-test-chart-and-check-the-install)
- [Logs and settings](#logs-and-settings)
- [Update](#update)
- [Compatibility](#compatibility)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Uninstall](#uninstall)

## Requirements

| You need | Notes |
|---|---|
| Rocksmith 2014 Remastered (Steam) | **September 2022 build** only. Learn & Play is a different program and is not supported. The mod checks the build at startup and does not patch other builds. A simple way to check your build yourself is still to be written. |
| A CDLC enabler for that build | Install it following its own instructions. It is still needed so the game accepts custom songs. |
| A seven-string guitar | With a working Rocksmith audio input. RS_ASIO is optional. |
| Seven-string charts | Plain PSARC packages (`*_p.psarc`) made for rs7string. See the [charting guide](CHARTING.md). |

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

> [!IMPORTANT]
> The download link will be added with the release.

1. Download the rs7string release zip and extract it to a folder **outside** the game folder. The
   files are in its `bin\Release` folder.
2. Copy these into the game folder (not into `dlc`):

   | File | What it is |
   |---|---|
   | `dinput8.dll` | The mod. |
   | `rs7string.ini` | Settings, every key at its default. |
   | `rs7string_headstock.bin` | Data for the headstock builder (right-handed, also shared textures). |
   | `rs7string_headstock_lh.bin` | Data for the headstock builder (left-handed). |
   | `rs7string_headstock.LICENSE.md` | Licence of the headstock data. |
   | `chord-diagrams` folder | Recipes for the seven-row chord diagrams. Copy the whole folder. |

> [!TIP]
> The `chord-diagrams` folder holds `commonlib.rs7da`, `contentpanel.rs7da`, `ge_game.rs7da`,
> `LICENSE` and `README.md`. With `diagrams7=1` (the default) the mod uses these recipes to patch the
> game's chord-diagram movies in memory. A movie that does not match the September 2022 build is left
> unchanged.

## 3. Headstock (built on first start)

The seven-string headstock model is **not** shipped. The headstock builder creates it from your own
game install:

- It runs once per hand, at the first guitar load. This can happen before you pick a song, and the
  first load can take a moment.
- It writes the model into the `rs7string\headstock` folder in the game folder. Later starts reuse it.
- It reads the game's files through the game's own loader and uses no keys.
- It needs `rs7string_headstock.bin` next to `dinput8.dll`. The left-handed model also needs
  `rs7string_headstock_lh.bin`.
- If the build fails, the stock headstock is used.

To skip the seven-string headstock, set `headstock7=0` under `[assets]` in `rs7string.ini`.

<details>
<summary><b>Log lines of the headstock builder</b></summary>

Text in square brackets is optional; paths and details vary.

| Log line | Meaning |
|---|---|
| `hsbuild: built in <dir>: <detail>` | Model built. |
| `hsbuild: cached in <dir>: <detail>` | Model already there from an earlier start. |
| `assetload: self-built [left-handed ]7-string model in use: <path>` | The model is shown in game. |
| `hsbuild: FAILED in <dir>: <reason>` | Build failed; the stock headstock is used. |
| `hsbuild: missing/unreadable <bin> next to DLL` | A `.bin` file is missing. |

Lines for the left-handed model have `left-handed ` before the result.

</details>

<details>
<summary><b>Advanced: use your own headstock model</b></summary>

Use this only if you want to supply your own model instead of the built one.

1. Close Rocksmith.
2. Copy your model and its textures into `rs7string\headstock`. Keep the textures beside the models.
3. In `rs7string.ini`, under `[assets]`, set `headstock7_file` (right-handed in-line model) and
   `headstock7_file_lh` (left-handed). Use absolute paths, or names relative to `[assets] dir`.
4. Save the file as `rs7string.ini`, not `rs7string.ini.txt`.

Rules:

- Alternate models for other stock headstocks are named `<dir>\<stock base><suffix>.nif`.
- Copying `rs7_peg.nif` alone does not replace the peg. The stock peg is used unless a file with the
  naming above is supplied. The headstock builder writes `rs7_peg.nif`, but the game does not
  necessarily load it.
- If a model file is missing, `selfbuild=1` (the default) builds the standard in-line model. If
  `selfbuild` is off or the build fails, the mod tries an edited file with the naming above, and
  otherwise keeps the stock model.
- An empty value, `none`, `off` or `0` for a model file key turns off the headstock builder for that hand.
- A separate authoring tool is not part of the release. The headstock builder itself needs no Python,
  uv or Blender.

</details>

## 4. Add a test chart and check the install

1. Close Rocksmith. Copy a known-good seven-string plain PSARC chart (`*_p.psarc`) into the game
   folder's `dlc` folder. Back up any package with the same name first. A test chart and its
   expected tuning are still to be linked.
2. Start Rocksmith through Steam. Press **F9** to open the rs7string overlay, then check the
   **Status** and **Log** tabs for errors.
3. Choose the seven-string Lead or Rhythm arrangement. For a B-standard chart, expect a tuning name
   such as `B Standard 7Str` and a low-B step in the tuner. Tune with your real guitar.
4. Play the chart. Check that the extra lane, the seventh string and the seven-peg headstock appear,
   and that low-B notes are detected.
5. Play an ordinary six-string arrangement as a control. It should look and play as before.

## Logs and settings

- **Log:** `rs7string.log`, next to `dinput8.dll`. It records the startup patch results and whether
  a loaded song uses seven-string mode.
- **Settings:** `rs7string.ini`, next to `dinput8.dll`. Missing keys use the defaults.
  [SETTINGS](SETTINGS.md) lists every key and whether it takes effect live, on restart, or at the next
  song.

## Update

1. Close Rocksmith.
2. Replace `dinput8.dll`, the two `.bin` files, `rs7string_headstock.LICENSE.md` and the
   `chord-diagrams` folder with the new release files. Keep your own `rs7string.ini` if you changed it.
3. Rename the `rs7string\headstock` folder to a backup name, so the headstock is rebuilt with the new
   version on the next start.

## Compatibility

| Component | Works with rs7string? |
|---|---|
| CDLC enabler | Required. Any enabler for the September 2022 build. |
| RSMods | Yes, with or without it (verified). It uses `xinput1_3.dll`. Check its keybindings if F9 is already taken. RSMods custom note colours can override the seventh-string gem colour. |
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
| The chart is missing or fails to load | The chart must be a PC plain PSARC in `dlc`, with a CDLC enabler and `[psarc] plain=1`. The log should have a `plain psarc: provided by rs7string` or `plain psarc: provided by the CDLC enabler` line. An enabler with its own plain PSARC support may log `Plain PSARC: pattern not found` when rs7string patched first; that is expected. Ask the chart author to check the seven-entry tuning and the `Rs7String6` key (see the [charting guide](CHARTING.md)). |
| The chart loads with six strings | The chart must be made for seven strings. Renaming a six-string file does not convert it. Check seven-string mode in the overlay or the log after the song loads. |
| Stock headstock, or no headstock build | Check that both `.bin` files are next to `dinput8.dll`, that `headstock7=1` and `selfbuild` is not `0` under `[assets]`, and the `hsbuild:` lines in the log. For your own model, check `headstock7_file` and that the `.dds` textures sit beside the model. Restart after changing asset settings. To retry a build, close the game and rename `rs7string\headstock`. |
| Wrong seventh-string gem colour | Check the `[color]` settings and `ramp_swap=1`. RSMods custom note colours can override the gem colours. |
| A song stops after about seven seconds with orange speaker rings | A game or audio-device issue. Check your normal Rocksmith audio setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| No sound or poor detection on all strings | First check your normal Rocksmith input setup with a six-string song. If you use RS_ASIO, check its settings and log too. |
| Arrow keys do not select tuner strings | Enter the precision view with Space or by playing a note. |
| A setting seems ignored | Some changes need a restart or a new tuner session; see [SETTINGS](SETTINGS.md). File edits generally need a restart. |

> [!TIP]
> **Reporting a bug:** include the mod version, game build, chart and arrangement, tuning, changed
> INI settings, other mods, the steps to reproduce, and the relevant lines from `rs7string.log`.
> Remove personal file paths and do not attach game files. The issue tracker link will be added with
> the release.

## Known limitations

| Area | Limitation |
|---|---|
| Local co-op | Only player 1 gets the seven-string guitar. If player 2 picks a seven-string arrangement, low-B notes cannot be hit. |
| Eight strings, extended bass | Not part of this release. Leave `[neck] eight=0` and do not install eight-string test charts. |
| Game version | Learn & Play is not supported. Seven-row chord diagrams only on the September 2022 build. |
| Chord diagrams | Some generic chords from the game's own chord database still show six rows. Highway chords always show the seventh string. |
| Chord matching | By default a seven-note chord may miss up to two notes, including low B. Use `[chords] wide_misses=0` for strict checks. |
| Tuner | After a seven-string song, the menu tuner shows seven pegs until the guitar returns to six strings (next six-string song, or Session Mode, Guitarcade, Lessons or the technique hub). Arrow keys work only in the precision view. |
| Tuning names | English only. Tuning filters and sorting still use the six-string tuning. Not every alternate tuning has a seven-string name. |
| Headstock | The in-line model is shown with the game's 6-in-line headstock option (German UI: "6 in Reihe"). With the 3+3 option a seven-string guitar shows the same in-line model and pegs; six-string guitars keep the stock 3+3 headstock. With the reversed string order, headstock strings 1-6 fade out before the posts do. |
| Overlay | Does not block all mouse input. A click on it can also reach the game. |
| Colours | Distant seventh-string gems can blend neighbouring colours on some textures. |

> [!NOTE]
> Real-guitar bends, slides, harmonics and chord techniques on the seventh string still need a final
> release check, as do bend reset after seeking or restarting and tuner thresholds in low tunings.

## Uninstall

1. Close Rocksmith.
2. From the game folder, delete `dinput8.dll`, `rs7string.ini`, `rs7string_headstock.bin`,
   `rs7string_headstock_lh.bin`, `rs7string_headstock.LICENSE.md` and the `chord-diagrams` folder.
   Delete only files you installed for this mod.
3. Restore any files you backed up in [step 1](#1-close-the-game-and-back-up).
4. Move seven-string charts out of `dlc`. The game without rs7string does not support their seventh
   string.
5. Optional: delete `rs7string.log`, `rs7string_vguitar.csv` (if present) and the `rs7string` folder
   with the built headstock.

Keep or remove the CDLC enabler, RSMods and RS_ASIO following their own instructions. They are
separate projects.
