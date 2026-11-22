# Manual pre-release test checklist

**What the maintainer checks by hand before a release, beyond the automated smoke test.**

[Contributing](../CONTRIBUTING.md) · [Development](DEVELOPMENT.md) · [Architecture](ARCHITECTURE.md) · [Extension block](architecture/extension-block.md) · [Chart tools](release/CHART-TOOLS.md) · [Releasing](RELEASING.md) · **Test checklist** · [Agents](../AGENTS.md)

Run this checklist after the gates in [RELEASING.md](RELEASING.md) pass. Use a copy of the game folder that you can
restore (see [docs/agents/docs-and-testing.md](agents/docs-and-testing.md#in-game-testing)). Each item says what to do,
then what to expect. If the docs do not define the expected result, the item says "check that", and you record what you see.

## Already covered by the smoke test

The maintainer's workspace script `scripts/smoke.py` (not in this repository) does these checks:

- It plays a seven-string and a six-string test song to their natural end.
- It takes frames of the seven- and six-string visual charts.
- It runs the pause-menu tuner and the main-menu tuner after a seven-string song.

Run it first, and do not repeat those checks here.

## Setup

1. Install the release zip into a fresh copy of the game folder ([INSTALL.md](release/INSTALL.md)). Check that
   `rsxr.log` shows no line saying a feature is disabled.
2. Put plain PSARC charts in `dlc` ([CHARTING.md](release/CHARTING.md)): a seven-string, an eight-string and a six-string
   guitar chart, a five-string and a six-string bass chart, and a four-string bass chart.

## Guitar types and display

3. **Left-handed profile**: play a seven-string song. Expect the left-handed in-line headstock. On the first start
   the log has `assetload: self-built left-handed 7-peg model in use`. Later starts load the file.
4. **Bass**: play a bass arrangement, a seven-string song, then bass again. Check that the guitar
   rebuilds each time, and that bass shows four strings with no seventh string.
5. **Eight strings**: play an eight-string chart, then a seven-string one. Expect eight string lines and the 8-peg
   headstock, then seven lines and the 7-peg headstock. The shipped `rsxr.ini` has no `[neck] eight` key.
   Then **extended bass**: play a five-string bass chart, then a six-string one, then a four-string one. Expect five
   strings with the low B, then six with the low B and high C. Each has its own in-line headstock, and notes on the
   extra strings are detected. Then expect the stock four strings with the headstock chosen in the game options.
6. **Colorblind mode**: turn it on in the game options and play a seven-string song. Expect the seventh
   string in `string6_color_cb` (gems, line, glow, tuner).
7. **Reversed string order**: turn it on. Expect the highway, fret bars and floor bars to follow it. Known issue:
   headstock strings 1-6 fade out before the posts.

## Headstocks (pegs and glow)

8. **In-line option** ("6-in-line", German "6 in Reihe"): open the tuner on a seven-string tuning. Expect
   seven pegs. The selected peg glows in its string's color, in the shape of the in-line peg.
9. **3+3 option**: same check. Expect the same in-line headstock and pegs, and a glow in the in-line peg
   shape, not the wider stock knob shape.
10. **Six-string guitar with 3+3**: play a six-string song. Expect the stock 3+3 headstock.
11. **Seventh peg color**: use a bright `string6_color`. Check that the peg glow keeps that color
    and does not turn cyan.

## RSMods variants

12. **RSMods absent** (remove its `xinput1_3.dll`): expect `string6_color` on string 7 and no RSMods errors in the log.
13. **Custom string colors on, mode 2**: set colors in `RSMods.ini`. Expect seventh-string notes in the
    seventh string's color (not grey or white) and the glow in sync with the selected tuner peg.
14. **Custom string colors off**: expect the stock string colors and `string6_color` for string 7.
15. **ExtendedRange=on**: with `[color] rsmods_extended_range=1`, expect low B `00C68E` in both color modes (in
    custom mode: a valid `string0_CB`). Set it to `0` in the overlay. Expect `string6_color` and
    `string6_color_cb`, with the saved values unchanged. RSMods changes need a game restart.
16. **Pure red low E** (for example `800000`, custom colors): expect a bright red glow outline when the
    low E is active. The log has `colour: RSMods custom glow correction=1`.
17. **Rainbow colors**: check that strings, colorblind mode, pegs and fret numbers take the right colors.

## CDLC enabler and plain PSARC

18. **Enabler without plain support**, `[psarc] plain=1`: check that a plain `*_p.psarc` chart is listed
    and plays.
19. **Enabler with plain support** (or `[debug] plain_delay_ms=3000`): check that the log says
    `provided by the CDLC enabler` and the chart loads once without errors.
20. **`[psarc] plain=0`**: RS Extended Range does not load plain charts itself. What to expect depends on the setup:
    - An enabler **with** plain support: plain charts still load. Do not judge by the `plain psarc:` log line,
      because it depends on whether RS Extended Range checks before or after the enabler has patched.
    - An enabler **without** plain support: remove the plain charts from `dlc` first. With plain charts installed,
      the game can abort after the profile is chosen. With none, it starts normally.
21. **Enabler console**: click in it during loading. Expect no freeze (`console_quickedit_off=1`).

## Chords

22. **Seven-note chords** with low B: play them. Expect the highway chord to span seven strings, and up to
    two missed notes to be accepted (`wide_misses=2`).
23. **Chord techniques**: play chord bends, sustains and chord frames with the seventh string. Check that they show.
24. **Chord diagrams**: expect seven rows with finger numbers and open-string bars. Known issue: some generic
    database chords show six rows. With `diagrams7=0`, check that the stock diagrams return.
25. **Six-string chords** in a six-string song: expect stock behavior.

## F9 overlay

26. **Open and close** with F9 (a changed `hotkey` needs a restart). With the overlay open, click, scroll and type
    in it during a song and in a menu. Expect none of it to reach the game: no menu action, no pause, no note.
27. **Tabs**: open every player tab, then **Advanced** and its Status, Colour, Neck, Tuner, Debug, Window and Log
    tabs. Check that each opens.
28. **Controls**: in each tab change every control once and put it back. Check that valid input is
    accepted and invalid input is rejected.
29. **Tooltips**: hover over every control. Expect a help text, "restart required" on Restart keys, and
    "changed, not saved (saved: ...)" on edited keys.
30. **Live keys** (Apply = Live in [SETTINGS.md](release/SETTINGS.md)): change `string6_color` during play. Check
    that the seventh string updates at once.
31. **Save**: the button reads "Save to rsxr.ini (N changed)" and is disabled at 0. Click it: expect
    "N key(s) saved".
32. **Status and Log tabs**: check that they match `rsxr.log`.

## Settings round trip

33. Close the game and open `rsxr.ini`. Expect only the changed keys to be rewritten, with the other keys and
    comments kept.
34. Start again. Check that the overlay shows the saved values and that the Restart keys are in effect.
35. Check missing, removed and old settings files and folders:
    - Delete `rsxr.ini`. Check that the defaults apply without errors.
    - Add a key from [Removed settings](release/SETTINGS.md#removed-settings). Expect `ignored, removed in 1.0`
      in the log.
    - Old file name: with only `rs7string.ini`, expect defaults and no log line saying the old file is read.
    - Save a change in the overlay. Expect a new `rsxr.ini` with the changed key. The old file's settings and
      comments are not copied, and `rs7string.ini` is unchanged.
    - Rename the old settings file to `rsxr.ini` and restart. Expect its settings to apply. With both files,
      only `rsxr.ini` is used.
    - Old models folder: with only `rs7string\headstock` and the default `[assets] dir`, expect the folder to be
      ignored. With the new `rsxr_headstock*.bin` packages installed, expect the headstock to be built in
      `rsxr\headstock`.
    - With only the `rs7string_headstock*.bin` packages and no built models, expect a missing-package message and
      the stock headstock. Copy the new packages and restart. Expect the extended headstock to build.

## Tuner

36. **Seven-string tunings** (B Standard, Drop A, A Standard): expect seven pegs, low B first, and names such as
    `B Standard 7Str`. With `[tuner] lowrange=1` (default), Drop A and G Standard should not warn about the
    instrument. With `lowrange=0` they may.
37. **Extended-range six-string tunings**: check that the six-string tuner shows six pegs and behaves as stock.
38. **After a seven-string song**: the main-menu tuner shows seven pegs (known issue). It does so until a
    six-string song, Session Mode, Guitarcade, Lessons or the technique hub returns the guitar to six strings.
    Check that.

## Other game modes

39. **Session Mode, Guitarcade, Lessons, technique hub**: after a seven-string song, enter each. Expect six
    strings and no crash.
40. **Two players**: player 1 on a seven-string arrangement, player 2 on a five-string bass arrangement (or the
    other way round). Expect each player's own strings, colors and scoring. Then swap arrangements.

## Uninstall

41. Close the game, remove `dinput8.dll` and the other installed files ([INSTALL.md](release/INSTALL.md#uninstall)).
    Expect stock play of a six-string song. Move extended charts out of `dlc`.

## Results

| # | Area | Pass / Fail | Notes (build, log line, frame) |
|---|---|---|---|
| 3-7 | Left-handed, bass, colorblind, reversed |  |  |
| 8-11 | Headstocks, peg glow |  |  |
| 12-17 | RSMods variants |  |  |
| 18-21 | CDLC enabler, plain PSARC |  |  |
| 22-25 | Chords and diagrams |  |  |
| 26-32 | F9 overlay |  |  |
| 33-35 | Settings round trip |  |  |
| 36-38 | Tuner |  |  |
| 39-40 | Other modes |  |  |
| 41 | Uninstall |  |  |

Version: _____  Commit: _____  Date: _____  Tester: _____
