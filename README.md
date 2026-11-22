# RS Extended Range

**Play seven- and eight-string guitar or five- and six-string bass in Rocksmith 2014 Remastered.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6.svg)](#requirements)
[![Game: Rocksmith 2014 Remastered PC](https://img.shields.io/badge/game-Rocksmith%202014%20Remastered%20PC-orange.svg)](#requirements)
[![Game build: September 2022](https://img.shields.io/badge/game%20build-Sept%202022-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/SomeeName/rs-extended-range?include_prereleases&label=release)](https://github.com/SomeeName/rs-extended-range/releases)
[![Docs](https://img.shields.io/badge/docs-someename.github.io%2Frs--extended--range-8A2BE2.svg)](https://someename.github.io/rs-extended-range/)

RS Extended Range (RSXR) adds support for seven- and eight-string guitars and five- and six-string basses to
Rocksmith 2014 Remastered. Seven-string guitars add a low B, and eight-string guitars add a low F# below it.
Five-string basses add a low B, and six-string basses add a high C as well.

The note highway shows all the strings, and the game detects and scores the notes you play on them. Six-string
guitar and four-string bass songs aren't affected.

You need charts made for the extra strings. RSXR doesn't convert existing six-string charts.

> [!TIP]
> **The documentation is also available as a [website](https://someename.github.io/rs-extended-range/)**, with
> search, dark mode, and separate starting points for players, chart makers, and developers. All the pages are also
> in this repository.
>
> Want to make your own charts? [DotCharter](https://github.com/SomeeName/DotCharter) is a free editor that can
> export seven- and eight-string guitar and five- and six-string bass arrangements for RSXR. See
> [Making charts](#making-charts).

## Start here

<div class="grid cards" markdown>

- **I want to play**

    ---

    Install RSXR, add an extended-range song, and change the settings if you need to.

    [Install guide](docs/release/INSTALL.md) · [Settings](docs/release/SETTINGS.md) · [FAQ](#faq)

- **I want to make charts**

    ---

    Learn how to make and test charts for seven- or eight-string guitar and five- or six-string bass.

    [Charting guide](docs/release/CHARTING.md) · [Chart tools](docs/release/CHART-TOOLS.md)

- **I want to work on the code**

    ---

    Learn how RSXR works in five minutes, then find build instructions, tests, and architecture notes.

    [Developer start](docs/GETTING-STARTED.md) · [Development](docs/DEVELOPMENT.md)

</div>

<img width="1922" height="1107" alt="Rocksmith gameplay with RS Extended Range" src="https://github.com/user-attachments/assets/f8ecad58-917e-4565-abb6-996a14270a64" />

<details class="github-only" markdown="1">
<summary><b>Contents</b></summary>

- [Start here](#start-here)
- [Features](#features)
- [Requirements](#requirements)
- [Quick install](#quick-install)
- [Known issues](#known-issues)
- [Making charts](#making-charts)
- [Settings](#settings)
- [FAQ](#faq)
- [How this project was made](#how-this-project-was-made)
- [Contributing](#contributing)
- [License](#license)
- [Support](#support)

</details>

## Features

- **Seven- and eight-string guitar:** The game detects and scores notes on every string. Alternate tunings work.
- **Five- and six-string bass:** A low B, and a high C on six-string bass, with matching strings, frets, and headstocks.
- **Local co-op:** Each player uses their own instrument. Player 2 can also play extended-range guitar or bass.
- **Low tunings:** Notes down to G0 are detected without workarounds.
- **More arrangements:** Songs can have more than five arrangements, shown in scrolling lists in single-player and co-op.
- **String colors:** The extra strings have their own colors and separate colorblind settings.
- **Fret grid:** Fret bars and the grid extend across all strings.
- **Tuner:** Starts on the lowest string. Song pages show tuning names such as `B Standard 7Str`.
- **Chord diagrams:** Show the extra strings, finger numbers, open-string bars, and technique visuals.
- **Headstocks:** RSXR builds matching seven-string, eight-string, and bass headstocks from your installed game files on first use. Right- and left-handed profiles are supported.
- **Reversed string order:** Works on the note highway, fret bars, and floor bars.
- **Unencrypted CDLC:** Loads plain (unencrypted) CDLC packages. Any CDLC enabler for the supported game build works alongside it.
- **CDLC console freeze fix:** Prevents freezes caused by clicking in the CDLC enabler's console.
- **F9 overlay:** Change settings and check the mod's status while the game is running.

For the full list of changes, see the [changelog](docs/release/CHANGELOG.md).

## Requirements

| You need | Details |
|---|---|
| Rocksmith 2014 Remastered for PC (Steam) | **September 2022 build only.** Learn & Play is not supported. RSXR checks the game build at startup. |
| A CDLC enabler for that build | A separate mod that lets Rocksmith load custom songs, called CDLC. For example, you can use [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix). RSXR does not replace it. |
| A seven- or eight-string guitar, or a five- or six-string bass | You also need a working Rocksmith audio input. |
| Extended-range charts | Charts must be made for RSXR. You can create them with [DotCharter](https://github.com/SomeeName/DotCharter). See [Making charts](#making-charts). |

> [!NOTE]
> [RSMods](https://github.com/Lovrom8/RSMods) and RS_ASIO are optional. RSMods adds other game tweaks, and RS_ASIO
> lets the game use ASIO audio interfaces. RSXR works with or without them, and both have been tested with it.
>
> macOS, Wine/Proton, and Steam Deck have not been tested.

## Quick install

Download the ZIP from the [latest release](https://github.com/SomeeName/rs-extended-range/releases/latest).

1. **Close Rocksmith.** In the folder that contains `Rocksmith2014.exe`, back up any existing `dinput8.dll`,
   `rsxr.ini`, and `chord-diagrams` folder. If `dinput8.dll` belongs to another mod, read the
   [compatibility notes](docs/release/INSTALL.md#compatibility) before you replace it.
2. **Install a CDLC enabler** for the September 2022 game build. Follow the enabler's own instructions.
3. **Extract the RSXR ZIP** somewhere outside your game folder. Open `bin\Release` and copy `dinput8.dll`,
   `rsxr.ini`, all four `rsxr_headstock*.bin` files, `rsxr_headstock.LICENSE.md`, and the `chord-diagrams` folder
   into your game folder.
4. **Add extended-range charts.** Copy the `*_rsxr_p.psarc` files into the game's `dlc` folder.
5. **Start Rocksmith and press F9.** In the RSXR overlay, open **Advanced > Status** to check that the mod is
   running.

The first time the game loads a guitar, RSXR builds the headstock models from your installed game files. It does the
same the first time you load an extended bass. This takes a moment, and only happens once.

For detailed instructions, checks, troubleshooting, and uninstall steps, see the [install guide](docs/release/INSTALL.md).

## Known issues

| Issue | Details or workaround |
|---|---|
| In local co-op with a guitar and a five- or six-string bass, some parts of the bass's low B use the guitar's B-string color. | This affects the string glow, fret wires, scale markers, and chord diagram dots. The bass string and its notes stay gray, as in single-player. It's only visual. |
| Some generic chords from the game's built-in chord database show only six rows. | Chords defined in the chart show all the strings. It's only visual. |
| Chord matching can accept low chords with missing notes. | By default, a seven- or eight-note chord can miss up to two notes. See `[chords] wide_misses` in [Settings](docs/release/SETTINGS.md). |
| The tuner's arrow keys only work in precision view. | Play a note or press Space to enter precision view. |
| With reversed string order, headstock strings 1-6 fade out before their posts. | It's only visual. |

See the install guide for the [full list of limitations](docs/release/INSTALL.md#known-limitations), and the
[architecture notes](docs/architecture/13-known-limitations-and-open-risks.md#13-known-limitations-and-open-risks-10)
for technical details.

## Making charts

Seven- and eight-string guitar charts use the game's normal Lead or Rhythm arrangements with more tuning entries.
Five- and six-string bass charts are marked as extended bass.

### With DotCharter (recommended)

[DotCharter](https://github.com/SomeeName/DotCharter) is a free charting editor and guitar game for Windows. You can
edit extended-range arrangements in it and export them in the format RSXR uses.

1. Download the latest [DotCharter release](https://github.com/SomeeName/DotCharter/releases/latest) and extract it.
2. Create or import a song. You can import audio, Guitar Pro, Editor on Fire, Rocksmith XML, and MIDI.
3. In the arrangement settings, set the string count and tuning, for example seven strings in B Standard.
4. Chart as usual. The extra strings appear as extra rows in the editor and on the 3D highway.
5. Export with **RS Extended Range**, then copy the `*_rsxr_p.psarc` file into the game's `dlc` folder.

DotCharter exports extended arrangements as plain (unencrypted) packages, and RSXR loads them alongside a CDLC
enabler. The charts use the [native format](docs/release/NATIVE-FORMAT.md), which stores each string by its position.

### Other tools

- **Chart authors:** Read the [charting guide](docs/release/CHARTING.md).
- **Tool authors:** Use the [native format specification](docs/release/NATIVE-FORMAT.md) for package, manifest, and chart layouts.
- **Older charts:** Use the converter in the release ZIP. See [Charts made for earlier versions](docs/release/CHARTING.md#charts-made-for-earlier-versions).

## Settings

Most settings live in `rsxr.ini`, next to `dinput8.dll` in your game folder. You can also change many of them in the
**F9** overlay.

[SETTINGS.md](docs/release/SETTINGS.md) lists every setting, its default, and when a change takes effect.

## FAQ

<details markdown="1">
<summary><b>Do I need RSMods or RS_ASIO?</b></summary>

No. Both are optional, and both have been tested with RSXR.

</details>

<details markdown="1">
<summary><b>Does it support eight-string guitar?</b></summary>

Yes. Eight-string charts have eight lines on the note highway, an eight-peg headstock, and tuning and scoring for the
eighth string. You don't need to turn anything on. Seven-string songs are unchanged.

</details>

<details markdown="1">
<summary><b>Does it support five- and six-string bass?</b></summary>

Yes. Five-string bass adds a low B, and six-string bass adds both a low B and a high C. The note highway and headstock
match the chart's string count. Four-string bass songs are unchanged.

</details>

<details markdown="1">
<summary><b>Does it work in local co-op?</b></summary>

Yes. Each player uses their own instrument and arrangement, and Player 2 can play extended-range guitar or bass too.
Notes, chords, tuning, and scoring work separately for each player.

</details>

<details markdown="1">
<summary><b>Can I play a six-string chart on a seven-string guitar?</b></summary>

No. RSXR needs a chart made for the extra strings. Renaming a six-string chart or changing its tuning doesn't convert
it.

</details>

<details markdown="1">
<summary><b>Does RSXR change game files or include Ubisoft assets?</b></summary>

No. RSXR doesn't include Ubisoft game assets or encryption keys. It builds the headstock models and chord diagrams
from your own installed game files, on your PC. **Don't share those generated files.** Other players build their own.

RSXR patches the game in memory and doesn't change the game's own files. To uninstall it, delete the files you added.

</details>

## How this project was made

I built RS Extended Range with a lot of help from [Claude Code](https://claude.com/claude-code), Anthropic's AI
coding agent.

AI agents reverse-engineered the game and wrote most of the code, tests, and documentation under my direction. I set
the goals, made the design decisions, and tested the results in Rocksmith with a real guitar.

The developer docs mark claims about game behavior as either verified (against the game's code or in the game) or
not yet verified.

Expect rough edges, and please report them.

## Contributing

Bug reports and pull requests are welcome.

- **Bug reports:** Open an [issue](https://github.com/SomeeName/rs-extended-range/issues) and include your `rsxr.log`.
  Remove personal paths first, and don't attach game files.
- **Code and docs:** Start with [CONTRIBUTING.md](CONTRIBUTING.md). Build notes are in
  [DEVELOPMENT.md](docs/DEVELOPMENT.md), and the design is in [ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **AI coding agents:** Read [AGENTS.md](AGENTS.md).
- **Security issues:** See [SECURITY.md](SECURITY.md).

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

RS Extended Range is released under the [MIT License](LICENSE).

- The [chord-diagram recipes](https://github.com/SomeeName/rs-extended-range/blob/main/data/chord-diagrams/LICENSE) and
  [headstock data](https://github.com/SomeeName/rs-extended-range/blob/main/data/headstock/LICENSE.md) have their own MIT licenses.
- Bundled libraries are in [third_party](https://github.com/SomeeName/rs-extended-range/tree/main/third_party). See [NOTICE.md](NOTICE.md).

Rocksmith is a trademark of Ubisoft. RS Extended Range is not affiliated with or endorsed by Ubisoft.

Thanks to the authors of the Rocksmith CDLC enablers, [RSMods](https://github.com/Lovrom8/RSMods), RS_ASIO, and Dear
ImGui.

## Support

RS Extended Range is free. If you like it, you can support its development on Ko-fi.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/A3U028PMFM)
