# 11. Window, input, overlay, logging

Part of the [rs7string architecture](../ARCHITECTURE.md).

These features handle the game window, the F9 overlay and diagnostic output. Test-only controls
are at the end.

| Area | Purpose |
|---|---|
| Console and window | Avoid console-selection stalls and optionally keep the game active in the background. |
| Overlay | Draw settings and status through the game's Direct3D device. |
| Logging | Record patch results, song state and selected diagnostics. |
| Test controls | Observe UI state and select variants for repeatable checks. |

## Console QuickEdit

`[window] console_quickedit_off=1` turns QuickEdit off on the console the CDLC enabler opens. With
QuickEdit on, a click starts a selection and every console write blocks; RSMods logs to stderr on the
game's main thread, so the game freezes until Enter.

## Background mode

With `[window] background=1`, `Win_GameWndProc` `0x78C9B0` tests `WM_ACTIVATEAPP`
at `0x78C9DC` and posts `0x7E9` on loss (CONFIRMED). The window subclass (installed at window
creation, independent of the overlay) swallows `WM_ACTIVATEAPP` with `wParam == FALSE` (returns 0),
`WM_ACTIVATE` with `WA_INACTIVE`, `WM_KILLFOCUS`, and `WM_NCACTIVATE` with FALSE (returns TRUE); it
never synthesizes activation. The foreground query (`GetForegroundWindow` as called from the exe on the
game thread, e.g. in `0xEC5D80`) is hooked to report the game window; other callers (overlay, DirectInput) see the
real value. HYPOTHESIS: the loss notification leaves audio/session state suspended, which was the
reported "song stops after seven seconds" symptom.

## Overlay

The F9 overlay (Dear ImGui 1.92.x, vendored unmodified in `third_party/imgui`, code in
`src/overlay/`) hooks vtable slots 16 (Reset) and 17 (Present) of the game's device, not EndScene
(RSMods inline-hooks EndScene and skips its own menu for calls not from the game exe). It draws
before the game's Present in its own BeginScene/EndScene and restores render target, depth buffer and
viewport. A fault in its callbacks switches it off.

Input: the game window is subclassed at the first Present. While the overlay uses the mouse or the
keyboard, the DirectInput wrapper hides that device's input from the game (`src/dinput_wrap.cpp`,
`src/overlay/capture.h`), so a click, wheel or key in the overlay does not also reach the game. The
command pipe is described in [DEVELOPMENT.md](../DEVELOPMENT.md).

## Logging

`rs7string.log` next to the DLL records patch results, per-song mode, ramp copies and feature lines.

Song end log (`[debug] song_end_log=1`): sites SE1 `0x7DD600`, SE2 `0x7DFA30` and SE3 `0x7E0AE6` log
each session state change. A song goes from state 3 (playing) to 4 (end) in exactly three ways: an
end-song event (type 3), the leave-state call, or a NULL song object while playing. A stray Enter
about two seconds into a song ends it about 7 s later through the first route, with the song clock
still at 0 (HYPOTHESIS: the stock game does the same).

## UI state

In `src/features/testdrive/uistate.*` (always on, log only), a thread polls the game's current UI state
every 100 ms and logs `ui: OLD -> NEW` (`MainMenu`, `LearnASong_Songs`, `LearnASong_SongOptions`,
`LearnASong_PreSongTuner`, `LearnASong_Game`, `LearnASong_Pause`, `LearnASong_SongReview`, `Tuner`, ...).
The state lives at global `0x135F62C` +28h, its name at +8Ch (CONFIRMED static: `0x766580` compares it
with `_stricmp`; in-game log 2026-10-17). In-game test drivers wait on these lines instead of fixed delays.
A screen's state is logged when it opens, before it takes keys (about 1.5 s of intro animation).

## Test speed-up

The test speed-up (`src/features/testdrive/fastmode.*`, `[test] fast=1`, default off) uses the
`fast` group in `sites.json`, resolved and written on its own after plain PSARC. `FST_REVIEW`
`0x623BC1` changes `jz` to `nop nop` in the after-song menu pick
`0x623B60`, so a song ends on `LearnASong_SongOptions`, not `LearnASong_SongReview` (CONFIRMED branch and in-game
run, about 19 s saved per song; whether the review screen saves anything the profile needs: HYPOTHESIS, not checked).
Not useful (measured 2026-10-17): a faster Flash clock (Scaleform `TimerOverride*` `0x133B19C`) or
Flash timeline (dt in the advance wrapper `0xC29E10`) does not shorten the menus' ~1.5 s key lock or
the boot.

## Test variants

Test variants live in `src/features/testdrive/variants.cpp` and `hooks_variants.cpp`. Their settings
are `[test] force_colourblind=on|off`, `force_lefty=lh|rh` (also `1` / `-1`) and
`force_headstock=33|6`, all unset by default; TVCB / TVGP are written only when one is set. The profile's settings
store holds `ColorBlindMode`, `LeftHandGuitar` and `GuitarTypeMP` (`6-IN-LINE` or not); the game copies them into
runtime objects, and only those copies get the forced values (CONFIRMED static):

- TVCB `0x519687` in `Rnd_ColorMgr_SetColorBlindMode` `0x519680`: EBX (a temporary copy of the setting text, from
  0x519750 or 0x601820) becomes `ColorBlindMild`, so the colour manager's mode (`+348h + 4 * player`) is 1.
  That alone does not last into a song (a `visual7cb` shot was pixel-identical to `visual7`): RSMods with
  `ExtendedRange=on` or `CustomStringColors=2` writes its Extended Range flag into player 0's mode byte on every draw
  call of a song (CONFIRMED by RSMods source: `GameState::ToggleCB` from `Hook_DIP` / `Hook_DP`, pointer chain
  `*0x135F58C +14h +24h +348h` = the manager's profile-option listener's manager, `Rnd_ColorMgr_Ctor` 0x517F70 /
  `0x52EC40`), so for a standard-tuned chart it is 0 again before the first frame's update reads it. With the key on,
  `Rs7Variant_OnFrame` (the overlay's Present callback, after the frame's draws, before the next update) writes the
  mode back to 1. Reads of the mode during a frame's draws after RSMods' first write still see RSMods' value
  (HYPOTHESIS: the string and gem colours are read in the update, `Rnd_GuitarString_Update` / `Rnd_GemColor_Update`).
  Without the overlay (no Present hook) only TVCB applies. The same RSMods write also overrides the real colourblind
  option in songs; that is RSMods' behaviour and is left alone when the key is off.
- TVGP `0x593D4A` in `Rnd_GuitarProfile_Load` `0x593B00` (ESI the runtime GuitarProfile): `+11h` lefty, `+13h`
  in-line (AL before its store) and `+15h` colourblind. `Rnd_InstrumentInfo_FromProfile` `0x58ABD0` copies +11h / +13h
  into InstrumentInfo `+9` / `+0Ah`, which the headstock frame choice and the assetload log read.

Nothing writes the store back from these copies: its setters run from the options menus (for example `0x891050`
sets `LeftHandGuitar` from the handedness option's text), so the owner's profile keeps its values. Not covered: UI
code that reads `LeftHandGuitar` from the store itself (for example the chord-diagram colours `0x75812F`).
