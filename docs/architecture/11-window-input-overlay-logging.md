# 11. Window, input, overlay, logging

Part of the [RS Extended Range architecture](../ARCHITECTURE.md).

These features handle the game window, the F9 overlay and diagnostic output. The test-only controls
are at the end.

| Area | Purpose |
|---|---|
| Console and window | Avoid console-selection stalls and optionally keep the game active in the background. |
| Overlay | Draw settings and status through the game's Direct3D device. |
| Logging | Record patch results, song state and selected diagnostics. |
| Test controls | Observe UI state and select variants for repeatable checks. |

## Console QuickEdit

`[window] console_quickedit_off=1` turns QuickEdit off on the console that the CDLC enabler opens. This
prevents a freeze. With QuickEdit on, a click starts a selection, and every console write then blocks.
RSMods logs to stderr on the game's main thread, so the game freezes until you press Enter.

## Background mode

With `[window] background=1`, the mod keeps the game running when its window loses focus.

`Win_GameWndProc` `0x78C9B0` tests `WM_ACTIVATEAPP` at `0x78C9DC` and posts `0x7E9` when the window loses
activation (CONFIRMED). The mod installs a window subclass at window creation, independent of the overlay.
The subclass swallows these messages:

- `WM_ACTIVATEAPP` with `wParam == FALSE` (returns 0)
- `WM_ACTIVATE` with `WA_INACTIVE`
- `WM_KILLFOCUS`
- `WM_NCACTIVATE` with FALSE (returns TRUE)

It never synthesizes activation. The mod also hooks the foreground query (`GetForegroundWindow` as called
from the exe on the game thread, for example in `0xEC5D80`) so that it reports the game window. Other
callers (the overlay, DirectInput) see the real value. HYPOTHESIS: the loss notification leaves the
audio/session state suspended, which caused the reported "song stops after seven seconds" symptom.

## Overlay

The F9 overlay uses Dear ImGui 1.92.x, vendored unmodified in `third_party/imgui`. Its code is in
`src/overlay/`. It hooks vtable slots 16 (Reset) and 17 (Present) of the game's device, not EndScene.
(RSMods inline-hooks EndScene and skips its own menu for calls that do not come from the game exe.) The
overlay draws before the game's Present, inside its own BeginScene/EndScene, and then restores the render
target, the depth buffer and the viewport. A fault in its callbacks switches it off.

The mod subclasses the game window at the first Present. While the overlay uses the mouse or the
keyboard, the DirectInput wrapper hides that device's input from the game (`src/dinput_wrap.cpp`,
`src/overlay/capture.h`). This way a click, wheel turn or key press in the overlay does not also reach
the game. The command pipe is described in [DEVELOPMENT.md](../DEVELOPMENT.md).

## Logging

`rsxr.log`, next to the DLL, records patch results, the per-song mode, ramp copies and feature lines.

The song end log (`[debug] song_end_log=1`) uses sites SE1 `0x7DD600`, SE2 `0x7DFA30` and SE3
`0x7E0AE6` to log each change of the session state. A song goes from state 3 (playing) to 4 (end) in
exactly three ways:

- an end-song event (type 3)
- the leave-state call
- a NULL song object while playing

A stray Enter about two seconds into a song ends it about 7 s later through the first route, with the
song clock still at 0. HYPOTHESIS: the stock game does the same.

## UI state

The UI state logger is in `src/features/testdrive/uistate.*`. It is always on and only writes to the log.
A thread polls the game's current UI state every 100 ms and logs `ui: OLD -> NEW` (`MainMenu`,
`LearnASong_Songs`, `LearnASong_SongOptions`, `LearnASong_PreSongTuner`, `LearnASong_Game`,
`LearnASong_Pause`, `LearnASong_SongReview`, `Tuner`, ...).

The state lives at global `0x135F62C` +28h, and its name at +8Ch (CONFIRMED static: `0x766580` compares it
with `_stricmp`; in-game log 2026-10-17). In-game test drivers wait for these lines instead of using fixed
delays. A screen's state is logged when the screen opens, before it accepts keys (about 1.5 s of intro
animation).

## Test speed-up

The test speed-up is in `src/features/testdrive/fastmode.*` and is controlled by `[test] fast=1` (default
off). It uses the `fast` group in `sites.json`, which the mod resolves and writes on its own, after plain
PSARC.

`FST_REVIEW` `0x623BC1` changes `jz` to `nop nop` in the after-song menu pick `0x623B60`. As a result, a
song ends on `LearnASong_SongOptions` instead of `LearnASong_SongReview` (CONFIRMED branch and in-game run).
This saves about 19 s per song. HYPOTHESIS, not checked: the review screen may save something the
profile needs.

Two other speed-ups did not help (measured 2026-10-17). They were a faster Flash clock (Scaleform `TimerOverride*` `0x133B19C`)
or a faster Flash timeline (dt in the advance wrapper `0xC29E10`). Neither shortens the ~1.5 s key lock
of the menus or the boot.

## Test variants

The test variants live in `src/features/testdrive/variants.cpp` and `hooks_variants.cpp`. Their settings
are `[test] force_colourblind=on|off`, `force_lefty=lh|rh` (also `1` / `-1`) and `force_headstock=33|6`.
All are unset by default. TVCB and TVGP are written only when one of them is set.

The profile's settings store holds `ColorBlindMode`, `LeftHandGuitar` and `GuitarTypeMP` (`6-IN-LINE` or
not). The game copies them into runtime objects, and only those copies get the forced values (CONFIRMED
static):

- **TVCB** `0x519687` in `Rnd_ColorMgr_SetColorBlindMode` `0x519680`: EBX (a temporary copy of the setting
  text, from 0x519750 or 0x601820) becomes `ColorBlindMild`, so the color manager's mode
  (`+348h + 4 * player`) is 1.

  That alone does not last into a song, because RSMods overwrites it. A `visual7cb` shot was
  pixel-identical to `visual7`. With `ExtendedRange=on` or `CustomStringColors=2`, RSMods writes its Extended Range flag into
  player 0's mode byte on every draw call of a song (CONFIRMED by RSMods source: `GameState::ToggleCB`
  from `Hook_DIP` / `Hook_DP`, pointer chain `*0x135F58C +14h +24h +348h` = the manager's profile-option
  listener's manager, `Rnd_ColorMgr_Ctor` 0x517F70 / `0x52EC40`). So for a standard-tuned chart the mode
  is 0 again before the first frame's update reads it.

  With the key on, `RsxrVariant_OnFrame` writes the mode back to 1. It is the overlay's Present callback,
  so it runs after the frame's draws and before the next update. Reads of the mode during a frame's draws,
  after RSMods' first write, still see RSMods' value (HYPOTHESIS: the string and gem colors are read in
  the update, `Rnd_GuitarString_Update` / `Rnd_GemColor_Update`).

  Without the overlay (no Present hook), only TVCB applies. The same RSMods write also overrides the real
  colorblind option in songs. That is RSMods' behavior, and the mod leaves it alone when the key is off.
- **TVGP** `0x593D4A` in `Rnd_GuitarProfile_Load` `0x593B00` (ESI is the runtime GuitarProfile): sets
  `+11h` lefty, `+13h` in-line (AL before its store) and `+15h` colorblind.
  `Rnd_InstrumentInfo_FromProfile` `0x58ABD0` copies +11h / +13h into InstrumentInfo `+9` / `+0Ah`. The
  headstock frame choice and the assetload log read those fields.

Nothing writes the store back from these copies. The store's setters run from the options menus (for
example, `0x891050` sets `LeftHandGuitar` from the text of the handedness option). So the owner's profile
keeps its values. Not covered: UI code that reads `LeftHandGuitar` from the store itself (for example the
chord-diagram colors `0x75812F`).
