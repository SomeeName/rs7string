# rs7string documentation

**Guides for players and chart authors. Start here if you downloaded the release zip.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

rs7string adds native seven-string guitar support to Rocksmith 2014 Remastered (Steam, September 2022
build, Windows). It plays charts made for seven strings; it does not convert six-string charts.

## Players

| Guide | What is in it |
|---|---|
| [INSTALL.md](INSTALL.md) | Requirements, install, first-start check, update, troubleshooting, known limitations, uninstall. |
| [SETTINGS.md](SETTINGS.md) | Every `rs7string.ini` key, its default and when it takes effect. |
| [CHANGELOG.md](CHANGELOG.md) | What is new in each version. |

## Chart and tool authors

| Guide | What is in it |
|---|---|
| [CHARTING.md](CHARTING.md) | How to make a seven-string chart, and how to test it. |
| [RS7X-SPEC.md](RS7X-SPEC.md) | Byte layout of the RS7X block for chords on the seventh string. |

## Maintainers

| Guide | What is in it |
|---|---|
| [RELEASING.md](RELEASING.md) | How a release is prepared and published. |

## Licence and game assets

- rs7string is MIT licensed. The chord-diagram recipes and the headstock data carry their own MIT
  licences (`chord-diagrams/LICENSE`, `rs7string_headstock.LICENSE.md`).
- The release ships no Ubisoft assets and uses no Ubisoft encryption keys.
- The headstock model and the chord diagrams are built from your own game install. Never share the
  generated files; each player builds their own.

> [!NOTE]
> The project overview and FAQ are in the [main README](../../README.md) on the project's GitHub page.
