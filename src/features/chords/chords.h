#pragma once
#include "model/extblock.h"
#include <cstdint>

struct Rs7HookTable;
struct Rs7SetTable;
// The chord loops over eight strings (group Chords): the CL* sites (registration.cpp).
extern const Rs7HookTable kRs7ChordHooks;
// The chord frame's string-axis span (B1 and the B2 stub of hooks_stubs.cpp, group Core).
extern const Rs7HookTable kRs7ChordFrameHooks;

// Chords with the 7th string: what is left outside the native N-slot chord path (features/nslot/nslot.h).
// Detection, the tracker, gems, open-string bars, tails, finger numbers, next-note targets and section masks all
// run through the game's own chord code over the T8/C8 records; this module only keeps
//  - the 7-string arrangement state (S4) and string 6's pitch for C++ users (N15),
//  - the chord frame's string-axis span (B1/B2): the frame model has a fixed six-string height and no
//    game data decides it (docs/ARCHITECTURE.md), so a frame of a chord that uses
//    string 6 is scaled to enclose the 7th lane,
//  - the codec of the old format (string 6 in name[31] of the template), still read for packages built before
//    the RS7X block (features/nslot/hooks.cpp builds T8 from it),
//  - the CL* sites: the stock chord loops over eight strings.
// Logic: chords.cpp (codec, ini parser). Glue: hooks.cpp (song state, chord frame B1/B2), hooks_cl.cpp (the CL*
// mid-hooks), hooks_loops.cpp (their string objects), hooks_frames.cpp (the B1 mid-hook), registration.cpp
// (kRs7ChordHooks, kRs7ChordFrameHooks).
namespace rs7chord {

constexpr uint32_t kTplFrets = 0x04;    // int8 frets[6]
constexpr uint32_t kTplFingers = 0x0A;  // int8 fingers[6]
constexpr uint32_t kTplMidi = 0x10;     // int32 midi[6]
constexpr uint32_t kTplName = 0x28;     // char name[32]
constexpr uint32_t kTplExt = 0x47;      // name[31]: the old-format string-6 column
constexpr uint32_t kTplSize = rs7ext::kTemplateSize;

struct Ext {
    int fret = -1;  // string 6 fret, -1 = no string-6 column
    int finger = -1;
};

// Old-format codec: byte = (finger6 + 1) << 5 | (fret6 + 1), 0 = none (fret 0..30, finger -1..6).
bool Decode(uint8_t b, Ext* out);
uint8_t Encode(int fret, int finger);  // 0 if fret is out of 0..30 (no column)
// Decodes a 0x48-byte template image: false unless name[0..30] holds a NUL and name[31] decodes.
bool FromTemplateBytes(const uint8_t* tpl, Ext* out);

}  // namespace rs7chord

// S4 (song setup, once per player): the matcher statistics of the last song, chord frame marks reset.
void Rs7Chord_OnSong();

// B1: a chord frame's geometry component was built for `chordObj` (template at +30h) on the highway `player`
// (guitar at +37Ch): every frame of a 7/8-string guitar or 5/6-string bass is marked to cover the whole instrument. B2:
// for a marked component, fills g_rs7_box_scale / g_rs7_box_trans (the stock span values plus the frame's string-axis
// scale and translation, derived from the guitar's lane positions: features/frets/frets.h ChordFrame) and returns 1; 0
// when the frame is exactly the stock one (stock ApplySpan). The string count and instrument are the highway's own
// guitar build (InstrumentInfo +0 / +0Bh), so in local co-op each player's frames follow their own instrument.
extern "C" __declspec(dllexport) void __cdecl Rs7Chord_OnBoxGeom(void* geom, const void* chordObj, const void* player);
extern "C" __declspec(dllexport) int __cdecl Rs7Chord_BoxScale(const void* geom);
extern "C" __declspec(dllexport) float g_rs7_box_scale[3];
extern "C" __declspec(dllexport) float g_rs7_box_trans[3];
// Sets g_rs7_match_misses (nslot.h) from [chords] wide_misses (0..2, default 2) at startup.
void Rs7Chord_Configure();

// The [chords] settings table; its apply hook takes live changes from the overlay (settings.cpp).
extern const Rs7SetTable kRs7ChordSettings;
// wide_misses in the player view's Guitar tab (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7ChordUi;

// Startup, after the native strings' copies: the CL* sites are written with the native strings
// (g_rs7_native_chordloops), logged.
void Rs7Chord_StartLoops(bool nativeStrings);

// CL* sites written (startup, and 0 when the Chords group fails).
extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_native_chordloops;
// Added strings only. Native owner, active/loaded slot and usable model are required.
// Uses GuitarStrings' own objects; does not mirror a stock string.
__declspec(dllexport) uint8_t* __cdecl Rs7ChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost);
}
