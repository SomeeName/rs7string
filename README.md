# rs7string

**Seven- and eight-string guitars and five- and six-string basses for Rocksmith 2014 Remastered.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6.svg)](#requirements)
[![Game: Rocksmith 2014 Remastered PC](https://img.shields.io/badge/game-Rocksmith%202014%20Remastered%20PC-orange.svg)](#requirements)
[![Game build: September 2022](https://img.shields.io/badge/game%20build-Sept%202022-lightgrey.svg)](#requirements)
[![Release](https://img.shields.io/github/v/release/SomeeName/rs7string?include_prereleases&label=release)](https://github.com/SomeeName/rs7string/releases)
[![Docs](https://img.shields.io/badge/docs-someename.github.io%2Frs7string-8A2BE2.svg)](https://someename.github.io/rs7string/)

rs7string is a small mod that adds extra strings to Rocksmith 2014 Remastered: a low B (and a low F# on eight
strings) for guitar, and a low B and high C for bass. You play on a highway with the right number of strings, and notes on the
extra strings are detected and scored like any other note. Six-string guitar and four-string bass songs are not
changed. The mod plays charts made for the extra strings; it does not convert six-string charts.

> [!TIP]
> **New: the documentation is now a website**, with search, a dark mode and a start page for players, chart makers
> and developers: **[someename.github.io/rs7string](https://someename.github.io/rs7string/)**. The same pages are
> also in this repository.
>
> Want to chart your own songs? [DotCharter](https://github.com/SomeeName/DotCharter) is a free charting editor
> that exports seven- and eight-string guitar and five- and six-string bass arrangements in the format rs7string
> plays. See [Making charts](#making-charts).

## Start here

<div class="grid cards" markdown>

- **I want to play**

    ---

    Install the mod, load your first extended song and set it up the way you like.

    [Install guide](docs/release/INSTALL.md) · [Settings](docs/release/SETTINGS.md) · [FAQ](#faq)

- **I want to make charts**

    ---

    What a chart for seven or eight strings or a five- or six-string bass needs, and how to test it.

    [Charting guide](docs/release/CHARTING.md) · [Chart tools](docs/release/CHART-TOOLS.md)

- **I want to work on the code**

    ---

    How the mod works in five minutes, then building, testing and the full architecture.

    [Developer start](docs/GETTING-STARTED.md) · [Development](docs/DEVELOPMENT.md)

</div>

<img width="1922" height="1107" alt="Rocksmith gameplay with rs7string" src="https://github.com/user-attachments/assets/f8ecad58-917e-4565-abb6-996a14270a64" />

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

- **Seven- and eight-string guitar** with note detection and scoring on every string. Alternate tunings work.
- **Five- and six-string bass** (low B, and a high C on the six-string) with its own strings, frets and headstock.
- **Local co-op**: each player gets their own instrument. Player 2 can play a seven- or eight-string guitar or a
  five- or six-string bass.
- **Low tunings** down to G0 are detected, with no workaround needed.
- **More than five arrangements per song**, in scrolling lists, in single player and in co-op.
- **Own colours for the extra strings**, with separate colourblind settings.
- **Fret grid and fret bars** that span all the strings.
- **Tuner** that starts on the lowest string. Song pages show names such as `B Standard 7Str`.
- **Chords with the extra strings**: extra-row chord diagrams, finger numbers, open-string bars and
  technique visuals.
- **Matching headstocks** (seven, eight or bass pegs), built on first start from your own installed game.
  Right- and left-handed profiles are supported.
- **Reversed string order** works on the highway, fret bars and floor bars.
- **Plain (unencrypted) CDLC packages load built in**: any CDLC enabler for the supported game build works.
- **Freeze fix** for clicks in the CDLC enabler's console.
- **F9 overlay** for settings and status while you play.

The full list is in the [changelog](docs/release/CHANGELOG.md).

## Requirements

| You need | Notes |
|---|---|
| Rocksmith 2014 Remastered for PC (Steam) | **September 2022 build** only. Learn & Play is not supported. The mod checks the build at startup. |
| A CDLC enabler for that build | A separate mod that lets the game load custom songs (CDLC), for example [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix). rs7string does not replace it. |
| A 7- or 8-string guitar, or a 5- or 6-string bass | And a working Rocksmith audio input. |
| Extended charts | Made for rs7string, for example with [DotCharter](https://github.com/SomeeName/DotCharter). See [Making charts](#making-charts). |

> [!NOTE]
> [RSMods](https://github.com/Lovrom8/RSMods) (a collection of game tweaks) and RS_ASIO (lets the game use
> ASIO audio interfaces) are optional. rs7string works with or without them, and both were tested together with it. macOS, Wine/Proton and Steam Deck have not been tested.

## Quick install

> [!IMPORTANT]
> Download the zip from the [latest release](https://github.com/SomeeName/rs7string/releases/latest).

1. Close Rocksmith and back up any existing `dinput8.dll`, `rs7string.ini` and `chord-diagrams` folder in the
   game folder (the folder with `Rocksmith2014.exe`). If `dinput8.dll` belongs to another mod, check
   [compatibility](docs/release/INSTALL.md#compatibility) before replacing it.
2. Install a CDLC enabler for the September 2022 build, following its own instructions.
3. Unzip the release outside the game folder. From its `bin\Release` folder, copy these files into the game folder: `dinput8.dll`, `rs7string.ini`,
   the four `rs7string_headstock*.bin` files, `rs7string_headstock.LICENSE.md` and the
   `chord-diagrams` folder.
4. Put charts for the extra strings (`*_p.psarc`) into the game's `dlc` folder.
5. Start the game and press **F9** to open the rs7string overlay (an in-game menu), then open
   **Advanced > Status** to check that the mod is running. The first time the game loads a guitar (and later an
   extended bass), the mod builds the headstocks from your own game files. That takes a moment, once.

Details, checks, troubleshooting and uninstall: [install guide](docs/release/INSTALL.md).

## Known issues

| Issue | What to do |
|---|---|
| Local co-op with a guitar and a five- or six-string bass: the low B's string glow, fret wires, scale markers and chord diagram dots use the guitar's B string colour. | Cosmetic. The low B's notes and string are grey, like in single-player bass. |
| Some generic chords from the game's chord database show six-row diagrams. | Cosmetic. Chords from the chart itself show every string. |
| Chord matching can accept low chords with missing notes. | A seven- or eight-note chord may miss up to two notes by default. See `[chords] wide_misses` in [SETTINGS](docs/release/SETTINGS.md). |
| Tuner arrow keys work only in the precision view. | Play a note or press Space to enter it. |
| With the reversed string order, headstock strings 1-6 fade out before the posts do. | Cosmetic. |

The install guide has the [full list of limits](docs/release/INSTALL.md#known-limitations).
Technical limits are in [ARCHITECTURE.md](docs/architecture/13-known-limitations-and-open-risks.md#13-known-limitations-and-open-risks-10).

## Making charts

Charts for seven or eight strings use the game's normal Lead or Rhythm arrangements with a longer tuning. Five- and
six-string bass charts are marked as extended bass.

### With DotCharter (recommended)

[DotCharter](https://github.com/SomeeName/DotCharter) is a free charting editor and guitar game for Windows. It
edits extended arrangements like any other and exports them in the layout rs7string reads.

1. Download the latest [DotCharter release](https://github.com/SomeeName/DotCharter/releases/latest) and unzip it.
2. Create or import a song (audio, Guitar Pro, Editor on Fire, Rocksmith XML or MIDI).
3. In the arrangement's settings, set the number of strings and the tuning, for example 7 strings in B Standard.
4. Chart as usual. The extra strings are extra rows in the editor and on the 3D highway.
5. Export as a Rocksmith package and copy the `*_p.psarc` into the game's `dlc` folder.

Extended arrangements are written as **plain (unencrypted) packages**, which rs7string loads built in next to a CDLC
enabler. Chords keep their notes on the extra strings (the [RS7X block](docs/release/RS7X-SPEC.md)).

### Other tools

- **Chart authors:** read the [charting guide](docs/release/CHARTING.md).
- **Tool authors:** start with the [chart tools guide](docs/release/CHART-TOOLS.md); the byte layout of the chord data is in the
  [RS7X specification](docs/release/RS7X-SPEC.md).
- **rs7chart**, the chart build and check tool, will be published later.

## Settings

Most settings live in `rs7string.ini` next to `dinput8.dll`. Many can also be changed in the
**F9** overlay. Every key, its default and when it takes effect: [SETTINGS.md](docs/release/SETTINGS.md).

## FAQ

<details markdown="1">
<summary><b>Does it work without RSMods or RS_ASIO?</b></summary>

Yes. Both are optional and were tested with rs7string.

</details>

<details markdown="1">
<summary><b>Does it support eight-string guitars?</b></summary>

Yes. Eight-string charts get eight string lines, an eight-peg headstock, the tuner and scoring for the eighth
string; no setting is needed. Seven-string songs are unchanged.

</details>

<details markdown="1">
<summary><b>Does it support five- and six-string bass?</b></summary>

Yes. A bass chart marked as five-string (low B) or six-string (low B and high C) gets a bass with that many strings
and a matching headstock. Four-string bass songs are unchanged.

</details>

<details markdown="1">
<summary><b>Does it work in local co-op?</b></summary>

Yes. Each player gets their own instrument. Player 2 can play a seven- or eight-string guitar or a five- or
six-string bass, and each player's notes, chords, tuner and scoring follow their own arrangement.

</details>

<details markdown="1">
<summary><b>Can it play my six-string charts on a seven-string guitar?</b></summary>

No. It plays charts made for the extra strings. Renaming or retuning a six-string chart does not convert it.

</details>

<details markdown="1">
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

- **Bug reports:** open an [issue](https://github.com/SomeeName/rs7string/issues) and include your `rs7string.log`. Remove personal paths and do not
  attach game files.
- **Code and docs:** read [CONTRIBUTING.md](CONTRIBUTING.md). Build notes are in
  [DEVELOPMENT.md](docs/DEVELOPMENT.md), the design in [ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **AI coding agents:** read [AGENTS.md](AGENTS.md).
- **Security issues:** see [SECURITY.md](SECURITY.md).

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

rs7string is released under the [MIT License](LICENSE).

- The [chord-diagram recipes](https://github.com/SomeeName/rs7string/blob/main/data/chord-diagrams/LICENSE) and the
  [headstock data](https://github.com/SomeeName/rs7string/blob/main/data/headstock/LICENSE.md) carry their own MIT licences.
- Bundled libraries are in [third_party](https://github.com/SomeeName/rs7string/tree/main/third_party). See [NOTICE.md](NOTICE.md).

Rocksmith is a trademark of Ubisoft. This project is not affiliated with or endorsed by Ubisoft.
Thanks to the authors of the Rocksmith CDLC enablers, [RSMods](https://github.com/Lovrom8/RSMods),
RS_ASIO and Dear ImGui.

## Support

rs7string is free. If you like it, you can support its development on Ko-fi.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/A3U028PMFM)
