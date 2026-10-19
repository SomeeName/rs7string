# Manual pre-release test checklist

**What the maintainer checks by hand before a release, beyond the automated smoke test.**

[Contributing](../../CONTRIBUTING.md) · [Development](../DEVELOPMENT.md) · [Architecture](../ARCHITECTURE.md) · [RS7X spec](RS7X-SPEC.md) · [Releasing](RELEASING.md) · **Test checklist** · [Agents](../../AGENTS.md)

Run this after the gates in [RELEASING.md](RELEASING.md) pass, on a copy of the game folder you can
restore (see [AGENTS.md](../../AGENTS.md#in-game-testing)). Each item is one line: do, then expect. Where
the docs do not settle the expected result, the item says "check that" and you record what you see.

## Already covered by the smoke test

The maintainer's workspace script `scripts/smoke.py` (not in this repository) plays a seven-string and a
six-string test song to their natural end, takes frames of the seven- and six-string visual charts, and
runs the pause-menu tuner and the main-menu tuner after a seven-string song. Run it first; do not repeat
those here.

## Setup

1. Install the release zip into a fresh copy of the game folder ([INSTALL.md](INSTALL.md)). Check that
   `rs7string.log` shows no line saying a feature is disabled.
2. Put a seven-string and a six-string plain PSARC chart in `dlc` ([CHARTING.md](CHARTING.md)). Do not
   install eight-string charts.

## Guitar types and display

3. **Left-handed profile**: play a seven-string song. Expect the left-handed in-line headstock; the log has
   `assetload: self-built left-handed 7-string model in use` (first start) or loads the file.
4. **Bass**: play a bass arrangement, a seven-string song, then bass again. Check that the guitar
   rebuilds each time and bass shows four strings with no seventh string.
5. **Eight strings**: not supported. Check that `[neck] eight` is `0` in the shipped `rs7string.ini`.
6. **Colourblind mode**: turn it on in the game options, play a seven-string song. Expect the seventh
   string in `string6_color_cb` (gems, line, glow, tuner).
7. **Reversed string order**: turn it on. Expect highway, fret bars and floor bars to follow it. Known:
   headstock strings 1-6 fade out before the posts.

## Headstocks (pegs and glow)

8. **In-line option** ("6-in-line", German "6 in Reihe"): open the tuner on a seven-string tuning. Expect
   seven pegs; the selected peg glows in its string's colour, in the shape of the in-line peg.
9. **3+3 option**: same check. Expect the same in-line headstock and pegs, and a glow in the in-line peg
   shape, not the wider stock knob shape.
10. **Six-string guitar with 3+3**: play a six-string song. Expect the stock 3+3 headstock.
11. **Seventh peg colour**: use a bright `string6_color`. Check that the peg glow keeps that colour
    (it turned cyan before beta.3).

## RSMods variants

12. **RSMods absent** (remove its `xinput1_3.dll`): expect `string6_color` on string 7, no RSMods errors in the log.
13. **Custom string colours on, mode 2**: set colours in `RSMods.ini`. Expect seventh-string notes in the
    seventh string's colour (not grey or white) and the glow in sync with the selected tuner peg.
14. **Custom string colours off**: expect the stock string colours and `string6_color` for string 7.
15. **ExtendedRange=on**: with `[color] rsmods_extended_range=1` expect low B `00C68E` (custom mode: a valid
    `string0_CB`) in both colour modes. Set it to `0` in the overlay: expect `string6_color` and
    `string6_color_cb`, saved values unchanged. RSMods changes need a game restart.
16. **Pure red low E** (for example `800000`, custom colours): expect a bright red glow outline when the
    low E is active; the log has `colour: RSMods custom glow correction=1`.
17. **Rainbow colours**: check that strings, colourblind mode, pegs and fret numbers take the right colours.

## CDLC enabler and plain PSARC

18. **Enabler without plain support**, `[psarc] plain=1`: check that a plain `*_p.psarc` chart is listed
    and plays.
19. **Enabler with plain support** (or `[debug] plain_delay_ms=3000`): check that the log says
    `provided by the CDLC enabler` and the chart loads once without errors.
20. **`[psarc] plain=0`**: check that a plain chart is no longer loaded and the game still starts.
21. **Enabler console**: click in it during loading. Expect no freeze (`console_quickedit_off=1`).

## Chords

22. **Seven-note chords** with low B: play them. Expect the highway chord to span seven strings and up to
    two missed notes accepted (`wide_misses=2`).
23. **Chord techniques**: chord bends, sustains and chord frames with the seventh string. Check that they show.
24. **Chord diagrams**: expect seven rows with finger numbers and open-string bars. Known: some generic
    database chords show six rows. With `diagrams7=0` check that the stock diagrams return.
25. **Six-string chords** in a six-string song: expect stock behaviour.

## F9 overlay

26. **Open and close** with F9 (a changed `hotkey` needs a restart). Known: a click on the overlay can reach the game.
27. **Tabs**: open Status, Colour, Neck, Tuner, Debug, Window and Log. Check that each opens.
28. **Controls**: in each tab change every control once and put it back. Check that valid input is
    accepted and invalid input is rejected.
29. **Tooltips**: hover over every control. Expect a help text, "restart required" on Restart keys and
    "changed, not saved (saved: ...)" on edited keys.
30. **Live keys** (Apply = Live in [SETTINGS.md](SETTINGS.md)): change `string6_color` during play. Check
    that the seventh string updates at once.
31. **Save**: the button reads "Save to rs7string.ini (N changed)" and is disabled at 0. Click it: expect
    "N key(s) saved".
32. **Status and Log tabs**: check that they match `rs7string.log`.

## Settings round trip

33. Close the game, open `rs7string.ini`. Expect only the changed keys rewritten, other keys and comments kept.
34. Start again. Check that the overlay shows the saved values and the Restart keys are in effect.
35. Delete `rs7string.ini`: check that defaults apply without errors. Add a key from
    [Removed settings](SETTINGS.md#removed-settings): expect `ignored, removed in 1.0` in the log.

## Tuner

36. **Seven-string tunings** (B Standard, Drop A, A Standard): expect seven pegs, low B first, names such as
    `B Standard 7Str`. `[tuner] lowrange=1` (default): Drop A and G Standard should not warn about the instrument; with
    `lowrange=0` they may.
37. **Extended-range six-string tunings**: check that the six-string tuner shows six pegs and behaves as stock.
38. **After a seven-string song**: the main-menu tuner shows seven pegs (known) until a six-string song,
    Session Mode, Guitarcade, Lessons or the technique hub returns the guitar to six strings; check that.

## Other game modes

39. **Session Mode, Guitarcade, Lessons, technique hub**: after a seven-string song enter each. Expect six
    strings and no crash.
40. **Two players**: the seventh string is not supported for player 2 (known). Check only for no crash.

## Uninstall

41. Close the game, remove `dinput8.dll` and the other installed files ([INSTALL.md](INSTALL.md#uninstall)).
    Expect stock play of a six-string song; move seven-string charts out of `dlc`.

## Results

| # | Area | Pass / Fail | Notes (build, log line, frame) |
|---|---|---|---|
| 3-7 | Left-handed, bass, colourblind, reversed |  |  |
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
