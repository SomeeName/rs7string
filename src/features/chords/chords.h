#pragma once
#include <cstdint>

struct RsxrHookTable;
struct RsxrSetTable;
// The chord loops over eight strings (group Chords): the CL* sites (registration.cpp).
extern const RsxrHookTable kRsxrChordHooks;
// The chord frame's string-axis span (B1 and the B2 stub of hooks_stubs.cpp, group Core).
extern const RsxrHookTable kRsxrChordFrameHooks;

// Chords with the 7th string: what is left outside the native N-slot chord path (features/nslot/nslot.h).
// Detection, the tracker, gems, open-string bars, tails, finger numbers, next-note targets and section masks all
// run through the game's own chord code over the T8/C8 records; this module only keeps
//  - the 7-string arrangement state (S4) and string 6's pitch for C++ users (N15),
//  - the chord frame's string-axis span (B1/B2): the frame model has a fixed six-string height and no
//    game data decides it (docs/ARCHITECTURE.md), so a frame of a chord that uses
//    string 6 is scaled to enclose the 7th lane,
//  - the CL* sites: the stock chord loops over eight strings.
// Glue: hooks.cpp (song state, chord frame B1/B2), hooks_cl.cpp (the CL*
// mid-hooks), hooks_loops.cpp (their string objects), hooks_frames.cpp (the B1 mid-hook), registration.cpp
// (kRsxrChordHooks, kRsxrChordFrameHooks).
// S4 (song setup, once per player): the matcher statistics of the last song, chord frame marks reset.
void RsxrChord_OnSong();

// B1: a chord frame's geometry component was built for `chordObj` (template at +30h) on the highway `player`
// (guitar at +37Ch): every frame of a 7/8-string guitar or 5/6-string bass is marked to cover the whole instrument. B2:
// for a marked component, fills g_rsxr_box_scale / g_rsxr_box_trans (the stock span values plus the frame's string-axis
// scale and translation, derived from the guitar's lane positions: features/frets/frets.h ChordFrame) and returns 1; 0
// when the frame is exactly the stock one (stock ApplySpan). The string count and instrument are the highway's own
// guitar build (InstrumentInfo +0 / +0Bh), so in local co-op each player's frames follow their own instrument.
extern "C" __declspec(dllexport) void __cdecl RsxrChord_OnBoxGeom(void* geom, const void* chordObj, const void* player);
extern "C" __declspec(dllexport) int __cdecl RsxrChord_BoxScale(const void* geom);
extern "C" __declspec(dllexport) float g_rsxr_box_scale[3];
extern "C" __declspec(dllexport) float g_rsxr_box_trans[3];
// Sets g_rsxr_match_misses (nslot.h) from [chords] wide_misses (0..2, default 2) at startup.
void RsxrChord_Configure();

// The [chords] settings table; its apply hook takes live changes from the overlay (settings.cpp).
extern const RsxrSetTable kRsxrChordSettings;
// wide_misses in the player view's Guitar tab (platform/uilayout.h).
struct RsxrUiTable;
extern const RsxrUiTable kRsxrChordUi;

// Startup, after the native strings' copies: the CL* sites are written with the native strings
// (g_rsxr_native_chordloops), logged.
void RsxrChord_StartLoops(bool nativeStrings);

// CL* sites written (startup, and 0 when the Chords group fails).
extern "C" {
__declspec(dllexport) extern uintptr_t g_rsxr_native_chordloops;
// Added strings only. Native owner, active/loaded slot and usable model are required.
// Uses GuitarStrings' own objects; does not mirror a stock string.
__declspec(dllexport) uint8_t* __cdecl RsxrChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost);
}
