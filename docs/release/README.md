# RS Extended Range documentation

**Start here if you downloaded the release zip.**

[README](../../README.md) · [Install](INSTALL.md) · [Settings](SETTINGS.md) · [Charting](CHARTING.md) · [Changelog](CHANGELOG.md)

RS Extended Range (RSXR for short) adds seven- and eight-string guitars and five- and six-string
basses to Rocksmith 2014 Remastered. It runs on the Steam version, September 2022 build, on Windows. You need charts
made for the extra strings, because the mod doesn't convert six-string guitar or four-string bass charts. The rest
of the game is unchanged.

## Play in five minutes

1. Close Rocksmith. The game folder is the folder with `Rocksmith2014.exe`. Back up any `dinput8.dll`, `rsxr.ini`
   and `chord-diagrams` folder that is already there. If `dinput8.dll` belongs to another mod, check
   [compatibility](INSTALL.md#compatibility) before you replace it.
2. Install a CDLC enabler for the September 2022 build if you don't have one yet. That's a separate mod that
   lets the game load custom songs, for example
   [RSCDLCEnabler-TooManyCoresFix](https://github.com/Lovrom8/RSCDLCEnabler-TooManyCoresFix).
3. From the zip's `bin\Release` folder, copy `dinput8.dll`, `rsxr.ini`, the four `rsxr_headstock*.bin` files,
   `rsxr_headstock.LICENSE.md` and the `chord-diagrams` folder into the game folder.
4. Put a chart made for your instrument (`*_rsxr_p.psarc`) in the game's `dlc` folder.
5. Start the game and press **F9** to open the RS Extended Range overlay. **Advanced > Status** shows that the mod is
   running. If the overlay doesn't open, see [troubleshooting](INSTALL.md#troubleshooting).

The first time the game loads a guitar or an extended bass, the mod builds the headstocks from your own
game files. This takes a moment and happens only once. The [Install guide](INSTALL.md) goes through every step in more detail
and shows how to check each one.

## What do you want to do?

| I want to... | Read |
|---|---|
| Install, update or remove the mod, or fix a problem | [Install guide](INSTALL.md), with requirements, checks, troubleshooting and known limits |
| Change colors, tuning names, the overlay key or the neck width | [Settings](SETTINGS.md), which lists every key and puts the common ones at the top |
| See what is new | [Changelog](CHANGELOG.md) |
| Make a chart for seven or eight strings or an extended bass | [Charting guide](CHARTING.md), with an overview, format rules, chords and testing |
| Convert charts made for earlier versions | [Chart converter](CHARTING.md#charts-made-for-earlier-versions) |
| Add extended-range export to a chart editor or package builder | [Native format](NATIVE-FORMAT.md) for package, manifest and chart layouts |
| Prepare a release (maintainers) | [Releasing](../RELEASING.md) |

## License and game assets

- RS Extended Range is MIT licensed. The chord-diagram recipes and the headstock data carry their own MIT
  licenses, in `chord-diagrams/LICENSE` and `rsxr_headstock.LICENSE.md`.
- `tools\rsxr-convert.exe` is this project's chart converter, `tools/rsxr_convert.py`, packed into one file
  with [PyInstaller](https://pyinstaller.org). It contains the Python runtime, which is under the
  [PSF license](https://docs.python.org/3/license.html).
- The release ships no Ubisoft assets and uses no Ubisoft encryption keys.
- The headstock model and the chord diagrams are built from your own game install. Never share the
  generated files. Each player builds their own.

> [!NOTE]
> The project overview and FAQ are in the [main README](../../README.md) on the project's GitHub page.
