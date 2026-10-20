# rs7string

**Native seven-string guitar support for Rocksmith 2014 Remastered.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6.svg)](#requirements)
[![Game: Rocksmith 2014 Remastered PC](https://img.shields.io/badge/game-Rocksmith%202014%20Remastered%20PC-orange.svg)](#requirements)
[![Game build: September 2022](https://img.shields.io/badge/game%20build-Sept%202022-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/SomeeName/rs7string?include_prereleases&label=release)](https://github.com/SomeeName/rs7string/releases)

rs7string is a small mod that adds a low B string to Rocksmith 2014 Remastered. You play
seven-string charts on a seven-string highway, and notes on the low B string are detected and scored
like any other note. Six-string songs and bass songs are not changed. The mod plays charts made for
seven strings; it does not convert six-string charts.

> [!TIP]
> Want to chart your own seven-string songs? [DotCharter](https://github.com/SomeeName/DotCharter)
> is a free charting editor that exports seven-string arrangements in the format rs7string plays.
> See [Making seven-string charts](#making-seven-string-charts).

<img width="1922" height="1107" alt="showcase" src="https://github.com/user-attachments/assets/f8ecad58-917e-4565-abb6-996a14270a64" />

<details>
<summary><b>Contents</b></summary>

- [Features](#features)
- [Requirements](#requirements)
- [Quick install](#quick-install)
- [Known issues](#known-issues)
- [Making seven-string charts](#making-seven-string-charts)
- [Settings](#settings)
- [FAQ](#faq)
- [How this project was made](#how-this-project-was-made)
- [Contributing](#contributing)
- [License](#license)
- [Support](#support)

</details>

## Features

- **Seven-string highway** with note detection and scoring on the low B string. Alternate tunings work.
- **Own colour for the seventh string** (light grey by default), with a separate colourblind setting.
- **Fret grid and fret bars** that span all seven strings.
- **Seven-string tuner** that starts on low B. Song pages show names such as `B Standard 7Str`.
- **Chords with the seventh string**: seven-row chord diagrams, finger numbers, open-string bars and
  technique visuals.
- **Seven-peg headstock**, built on first start from your own installed game. Right- and left-handed
  profiles are supported.
- **Reversed string order** works on the highway, fret bars and floor bars.
- **Plain PSARC loading built in**: any CDLC enabler for the supported game build works.
- **Freeze fix** for clicks in the CDLC enabler's console.
- **F9 overlay** for settings and status while you play.

The full list is in the [changelog](docs/release/CHANGELOG.md).

## Requirements

| You need | Notes |
|---|---|
| Rocksmith 2014 Remastered for PC (Steam) | **September 2022 build** only. Learn & Play is not supported. The mod checks the build at startup. |
| A CDLC enabler for that build | Still needed so the game accepts custom songs. rs7string does not replace it. |
| A seven-string guitar | And a working Rocksmith audio input. |
| Seven-string charts | Made for rs7string, for example with [DotCharter](https://github.com/SomeeName/DotCharter). See [Making seven-string charts](#making-seven-string-charts). |

> [!NOTE]
> RSMods and RS_ASIO are optional. rs7string works with or without them, and both were tested
> together with it. macOS, Wine/Proton and Steam Deck have not been tested.

## Quick install

> [!IMPORTANT]
> The download link will be added with the release.

1. Close Rocksmith and back up any existing `dinput8.dll` in the game folder (the folder with
   `Rocksmith2014.exe`).
2. Install a CDLC enabler for the September 2022 build, following its own instructions.
3. Copy these release files into the game folder: `dinput8.dll`, `rs7string.ini`,
   `rs7string_headstock.bin`, `rs7string_headstock_lh.bin`, `rs7string_headstock.LICENSE.md` and the
   `chord-diagrams` folder.
4. Put seven-string charts (`*_p.psarc`) into the game's `dlc` folder.
5. Start the game. On first start the headstock builder creates the seven-string headstock from your
   own install. Press **F9** to check the status.

Details, checks, troubleshooting and uninstall: [install guide](docs/release/INSTALL.md).

## Known issues

| Issue | What to do |
|---|---|
| Local co-op: only player 1 gets the seven-string guitar. | Player 2 plays with six strings. Low-B notes cannot be hit by player 2. |
| Eight-string charts are not supported yet. | Leave `[neck] eight=0`. |
| Some generic chords from the game's chord database show six-row diagrams. | Cosmetic. Chords from the chart itself show seven rows. |
| Chord matching can accept low chords with missing notes. | A seven-note chord may miss up to two notes by default. See `[chords] wide_misses` in [SETTINGS](docs/release/SETTINGS.md). |
| Tuner arrow keys work only in the precision view. | Play a note or press Space to enter it. |
| The overlay does not block mouse input. | A click on the overlay can also reach the game. |
| With the reversed string order, headstock strings 1-6 fade out before the posts do. | Cosmetic. |

The install guide has the [full list of limits](docs/release/INSTALL.md#known-limitations).
Technical limits are in [ARCHITECTURE.md](docs/ARCHITECTURE.md#14-known-limitations-and-open-risks-10).

## Making seven-string charts

Seven-string charts use the game's normal Lead or Rhythm arrangements with a seven-entry tuning.

### With DotCharter (recommended)

[DotCharter](https://github.com/SomeeName/DotCharter) is a free charting editor and guitar game for
Windows. It edits seven-string guitar arrangements like any other and exports them in the layout
rs7string reads.

1. Download the latest [DotCharter release](https://github.com/SomeeName/DotCharter/releases/latest)
   and unzip it.
2. Create or import a song (audio, Guitar Pro, Editor on Fire, Rocksmith XML or MIDI).
3. In the arrangement's settings, set **Strings** to 7 and the tuning, for example B Standard.
4. Chart as usual. The low B string is the seventh row in the editor and on the 3D highway.
5. Export as a Rocksmith package and copy the `*_p.psarc` into the game's `dlc` folder.

Good to know:

- Seven-string arrangements are written as **plain (unencrypted) packages**. rs7string's built-in
  plain-PSARC loading plays them together with a CDLC enabler for the September 2022 build.
- Chord notes on the seventh string are stored in the song file's **RS7X block**, so chords stay
  chords. For an rs7string build from before the RS7X block, turn on **Split 7th-string chords** in
  the export dialog; those chord notes are then written as single notes.
- Seven-string arrangements play on **Windows only**. Mac packages are six-string only.
- Eight-string guitar arrangements are recognised, but not exported yet; rs7string plays seven
  strings.
- Bass arrangements and six-string arrangements in the same song export normally.

### By hand or with your own tools

- **Chart authors:** read the [charting guide](docs/release/CHARTING.md).
- **Tool authors:** the chord data format is in the [RS7X specification](docs/release/RS7X-SPEC.md).
- **rs7chart**, the chart build and check tool, will be published later.

## Settings

Most settings live in `rs7string.ini` next to `dinput8.dll`. Many can also be changed live in the
**F9** overlay. Every key, its default and when it takes effect: [SETTINGS.md](docs/release/SETTINGS.md).

## FAQ

<details>
<summary><b>Does it work without RSMods or RS_ASIO?</b></summary>

Yes. rs7string works with or without them. This was verified in game. Both were also tested together
with rs7string.

</details>

<details>
<summary><b>Does it support eight-string guitars?</b></summary>

Not yet. Eight-string support is prepared internally but not playable. Leave `[neck] eight=0`.

</details>

<details>
<summary><b>Does it work in local co-op?</b></summary>

Only player 1 gets the seven-string guitar. Player 2 plays with six strings.

</details>

<details>
<summary><b>Can it play my six-string charts on a seven-string guitar?</b></summary>

No. It plays charts made for seven strings. Renaming or retuning a six-string chart does not convert it.

</details>

<details>
<summary><b>How do I make my own seven-string charts?</b></summary>

Use [DotCharter](https://github.com/SomeeName/DotCharter): set the arrangement to seven strings,
chart it, and export a Rocksmith package. See [Making seven-string charts](#making-seven-string-charts).

</details>

<details>
<summary><b>Is it safe and legal?</b></summary>

- rs7string ships no Ubisoft assets and uses no Ubisoft encryption keys.
- The headstock model and the chord diagrams are built from **your own** game install on your PC.
- Never share those generated files. Each player builds their own.
- The mod patches the game in memory. It does not modify the game's own files; to uninstall, delete
  the files you added.

</details>

## How this project was made

rs7string was built by its owner (SomeeName) with heavy help from
[Claude Code](https://claude.com/claude-code), Anthropic's AI coding agent.

- **AI agents** did the reverse engineering of the game and wrote most of the code, the tests and
  the documentation, under the owner's direction.
- **The owner** set the goals, made the design decisions, and tested the result with a real guitar in
  the real game.
- **Claims about game behaviour** in the developer docs are tagged as checked (against the game's
  code or in game) or as not yet verified.

Expect rough edges, and please report them.

## Contributing

Bug reports and pull requests are welcome.

- **Bug reports:** open an issue and include your `rs7string.log`. Remove personal paths and do not
  attach game files.
- **Code and docs:** read [CONTRIBUTING.md](CONTRIBUTING.md). Build notes are in
  [DEVELOPMENT.md](docs/DEVELOPMENT.md), the design in [ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **AI coding agents:** read [AGENTS.md](AGENTS.md).
- **Security issues:** see [SECURITY.md](SECURITY.md).

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

rs7string is released under the [MIT License](LICENSE).

- The [chord-diagram recipes](data/chord-diagrams/LICENSE) and the
  [headstock data](data/headstock/LICENSE.md) carry their own MIT licences.
- Bundled libraries are in [third_party](third_party). See [NOTICE.md](NOTICE.md).

Rocksmith is a trademark of Ubisoft. This project is not affiliated with or endorsed by Ubisoft.
Thanks to the authors of the Rocksmith CDLC enablers, [RSMods](https://github.com/Lovrom8/RSMods),
RS_ASIO and Dear ImGui.

## Support

rs7string is free. If you like it, you can support its development on Ko-fi.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/A3U028PMFM)
