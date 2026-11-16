# rs7string documentation

**Start here if you downloaded the release zip.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

rs7string adds seven- and eight-string guitars and five- and six-string basses to Rocksmith 2014 Remastered
(Steam, September 2022 build, Windows). It plays charts made for those instruments; it does not convert six-string
guitar or four-string bass charts. Everything else in the game stays as it was.

## Play in five minutes

1. **Close Rocksmith** and back up any `dinput8.dll`, `rs7string.ini` and `chord-diagrams` folder in the game
   folder (the folder with `Rocksmith2014.exe`). If `dinput8.dll` belongs to another mod, check
   [compatibility](INSTALL.md#compatibility) before replacing it.
2. **Install a CDLC enabler** for the September 2022 build, if you do not have one yet. It is a separate mod that
   lets the game load custom songs, for example
   [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix).
3. **Copy the mod files** from the zip's `bin\Release` folder into the game folder: `dinput8.dll`, `rs7string.ini`, the four
   `rs7string_headstock*.bin` files, `rs7string_headstock.LICENSE.md` and the `chord-diagrams` folder.
4. **Add a chart** made for your instrument (`*_p.psarc`) to the game's `dlc` folder.
5. **Start the game** and press **F9** to open the rs7string overlay. **Advanced > Status** shows that the mod is
   running. No overlay? See [troubleshooting](INSTALL.md#troubleshooting).

The first time the game loads a guitar (and later an extended bass), the mod builds the headstocks from your own
game files. That takes a moment, once. Every step in detail, with checks: [Install guide](INSTALL.md).

## What do you want to do?

| I want to... | Read |
|---|---|
| Install, update or remove the mod, or fix a problem | [Install guide](INSTALL.md): requirements, checks, troubleshooting, known limits |
| Change colours, tuning names, the overlay key or the neck width | [Settings](SETTINGS.md): every key, with the few most people change at the top |
| See what is new | [Changelog](CHANGELOG.md) |
| Make a chart for seven or eight strings or an extended bass | [Charting guide](CHARTING.md): an overview, format rules, chords and testing |
| Add extended-range export to a chart editor or package builder | [Chart tools](CHART-TOOLS.md), then the [RS7X specification](RS7X-SPEC.md) for the byte layout |
| Prepare a release (maintainers) | [Releasing](../RELEASING.md) |

## Licence and game assets

- rs7string is MIT licensed. The chord-diagram recipes and the headstock data carry their own MIT
  licences (`chord-diagrams/LICENSE`, `rs7string_headstock.LICENSE.md`).
- The release ships no Ubisoft assets and uses no Ubisoft encryption keys.
- The headstock model and the chord diagrams are built from your own game install. Never share the
  generated files; each player builds their own.

> [!NOTE]
> The project overview and FAQ are in the [main README](../../README.md) on the project's GitHub page.
