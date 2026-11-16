#pragma once
#include "platform/sites.h"
#include <cstddef>
#include <cstdint>

// Aids for the in-game test drivers (scripts/rsgame.py): shorter runs ([test] fast) and the UI state log.

// rs7string.ini [test] fast=1 (test only, default 0): shorter runs for in-game test drivers (scripts/rsgame.py).
// FST_REVIEW 0x623BC1 (sites.json group "fast", kRs7FastSites): jz -> nop in the after-song menu pick 0x623B60, so a
// song ends on its song page (LearnASong_SongOptions) instead of the results screen (LearnASong_SongReview).
//
// Tried and dropped (measured 2026-10-17): running the Flash clock (Scaleform TimerOverride 0x133B19C) or the Flash
// timeline (advance wrapper 0xC29E10) faster does not shorten the menus' ~1.5 s key lock or the boot.

// Reads [test] fast; with fast=1 resolves the fast sites and writes them. Logs either way.
void Rs7Fast_Start(const wchar_t* iniPath, const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize);

// The game's current UI state ("MainMenu", "LearnASong_Songs", "LearnASong_Game", "LearnASong_Pause", ...), logged
// as "ui: OLD -> NEW" on every change. In-game test drivers wait on these lines instead
// of fixed delays and navigate from wherever the game is.
//
// CONFIRMED static (0x766580 compares it with _stricmp against "ProfileSelect", "LearnASong_Game", "Tuner", ...):
// global 0x135F62C -> +28h the current state object -> +8Ch its name, a string with the layout of 0x4073C0:
// +0 the inline buffer or the heap pointer, +10h the end pointer, +14h == &+10h when the text is inline.
constexpr uint32_t kUiStateGlobal = 0x135F62C, kUiStateCurrent = 0x28, kUiStateName = 0x8C;

// Polls the state every 100 ms on its own thread (log only, nothing is patched).
void Rs7Ui_Start();

// The current state's name of the manager pointed to by `global` into `out` (cap > 0); false (out = "") when
// there is no manager or state, the memory is unreadable or the name is empty, too long or not printable ASCII.
bool Rs7Ui_ReadState(uintptr_t global, char* out, size_t cap);

// rs7string.ini [test] force_colourblind=0|1|-1 (also on / off; -1: normal colours), force_lefty=0|1|-1 (also lh /
// rh; -1: right-handed), force_headstock=33|6
// (test only, default off; the TV* hooks are written only when one is on): runtime overrides of the profile's
// ColorBlindMode, LeftHandGuitar and GuitarTypeMP, so in-game test drivers (scripts/smoke.py visual7cb / visual7lefty /
// visual7hs33 / visual7hs6) cover these variants without the owner. Only the game's runtime copies get the forced
// values; the profile's settings store is never written (CONFIRMED static: the copies are filled by reads only,
// Rnd_GuitarProfile_Load 0x593B00 and Rnd_ColorMgr_SetColorBlindMode 0x519680; the store's setters run from the options
// menus, e.g. 0x891050). Not overridden: UI code that reads LeftHandGuitar from the store itself (e.g. chord-diagram
// colours 0x75812F).
struct Rs7Variant {
    bool colourblind = false;
    bool normalColours =
        false;  // force_colourblind=-1: normal colours (a colourblind profile); no per-frame write-back
    bool lefty = false;
    bool righty = false;  // force_lefty=-1: right-handed (a left-handed profile)
    int headstock = 0;    // 0 the profile's, 33 three-plus-three, 6 six in-line

    bool Any() const { return colourblind || normalColours || lefty || righty || headstock != 0; }
};

// The GuitarProfile flags the overrides touch (+11h lefty, +13h in-line headstock, +15h colourblind).
struct Rs7GuitarFlags {
    uint8_t lefty;
    uint8_t inlineHead;
    uint8_t colourblind;
};

// Pure: a three-way override's text: "" / "0" -> 0 (the profile's), "1" / `on` -> 1, "-1" / `off` -> -1, else -2
// (force_lefty: on "lh", off "rh"; force_colourblind: "on" / "off"; the words case-insensitive).
int Rs7Variant_ParseForce(const wchar_t* text, const wchar_t* on, const wchar_t* off);
// Pure: force_headstock's text: "" / "0" -> 0, "33" / "3+3" -> 33, "6" / "inline" / "6-in-line" -> 6, else -1.
int Rs7Variant_ParseHeadstock(const wchar_t* text);
// Pure: the profile's flags with the overrides of `v` applied.
Rs7GuitarFlags Rs7Variant_Apply(const Rs7Variant& v, Rs7GuitarFlags profile);
// Pure: the colour-mode text the game gets: "ColorBlindMild" when forced, else `profile`.
const char* Rs7Variant_ColourModeText(const Rs7Variant& v, const char* profile);
// Pure: the colour manager's mode (+348h + 4 * player, 0 normal, 1 colourblind) a frame should start with: 1 when
// forced, else `current`.
uint32_t Rs7Variant_ColourMode(const Rs7Variant& v, uint32_t current);
// Once per frame (the overlay's Present callback, the game/render thread), with force_colourblind only: writes the
// colour manager's mode back to colourblind when something reset it. RSMods (xinput1_3.dll) with ExtendedRange=on or
// CustomStringColors=2 writes its own Extended Range flag into that mode on every draw call of a song (CONFIRMED by
// RSMods source: GameState::ToggleCB, pointer chain *0x135F58C +14h +24h +348h, from Hook_DIP / Hook_DP), so the
// one-shot TVCB value lasts only until the song's first frame. Present runs after the frame's draws and before the
// next frame's update, which reads the mode (string colours, gem UVs). Without the overlay (no Present hook) only
// TVCB applies.
void Rs7Variant_OnFrame();
// Reads the three keys once at startup (before the sites are written) and logs them.
void Rs7Variant_ReadIni(const wchar_t* iniPath);
const Rs7Variant& Rs7Variant_Get();

struct Rs7HookTable;
extern const Rs7HookTable kRs7TestVariantHooks;  // TVCB, TVGP (registration.cpp)
