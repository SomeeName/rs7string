#pragma once
#include "features/nslot/nslot.h"
#include <cstdint>

// Native N-slot chord matcher. Play_Matcher_DetectChord 0x4E7B30 (stdcall
// (matcher, int32 targets[], tolerance, history index), AL = matched) takes eight targets: every caller
// hands it an int32[8] (strings 0-7, -1 = none; nslot.h). MD1 (its entry) calls
// RsxrNs_DetectChord:
//  - at most six non-negative targets: they are compacted (order kept, the -1 holes dropped) into
//    out6 and the stock body runs on them (returns -1). Dropping holes changes nothing the stock
//    algorithm looks at (dedupe, active count, lowest target, subharmonic rows, OctaveDyad), so these
//    chords are judged bit for bit as before;
//  - seven or eight: rsxrmatch::MatchN, the stock algorithm over n targets (returns 0/1);
//  - a LowChord (two or three notes, the lowest below E1): the port too, its lowest note matching on
//    its fundamental or its octave (stock: fundamental only; returns 0/1).
// docs/ARCHITECTURE.md has the port's derivation.
extern "C" __declspec(dllexport) int __cdecl RsxrNs_DetectChord(const uint8_t* matcher, const int32_t* in8, int tol,
                                                                int hist, int32_t* out6);

namespace rsxrmatch {
constexpr int kMaxTargets = 8;

// Addresses bound by Rsxr_Startup from sites.json (X_MSORT, X_MINS1, X_MINS2: the std::sort pieces the
// stock matcher calls; X_MCMP: its comparator; X_MTAB: the subharmonic table 0x119A398).
struct Game {
    uintptr_t sort = 0, ins1 = 0, ins2 = 0, cmp = 0, table = 0;
    int misses = 2;     // seven/eight targets: how many may be missing ([chords] wide_misses)
    int lowOctave = 0;  // the special row's target below this MIDI also matches on its octave (0 = stock)
};

// Below E1 (MIDI 28, the lowest note of a stock bass) a string sounds mostly at its octave: in a chord of
// two or three notes the lowest one (stock: its fundamental only) may also match on its octave.
constexpr int kLowOctave = 28;

// The stock algorithm over n (<= 8) targets. Pure apart from the game memory it reads (matcher,
// detector history frame, subharmonic table) and the game's sort routines.
int MatchN(const uint8_t* matcher, const int32_t* in, int n, int tol, int hist, const Game& g);
// 0x4E8650: exactly two positive targets and the second is the first + 12 (an octave dyad).
bool OctaveDyad(const int32_t* in, int n);
// Two or three different targets and the lowest is below kLowOctave (the chords the octave rule changes).
bool LowChord(const int32_t* in, int n);
// Compacts the non-negative entries of in[0..n) into out6; false if there are more than six.
bool Compact6(const int32_t* in, int n, int32_t* out6);
// The subharmonic table 0x119A398 has rows of six int32 for MIDI 22-93 only (docs/architecture/13). CONFIRMED: the
// game's own lookups bound it the same way (0x4DFCF0: midi - 22, unsigned compare with 47h).
constexpr int kTableFirst = 22;
constexpr int kTableRows = 72;
// The table row of `midi`, or null outside the table (the stock matcher indexes it unchecked; a very low extra
// string fails closed: its target has no row and is never found).
const int32_t* TableRow(uintptr_t table, int32_t midi);
}  // namespace rsxrmatch

extern "C" __declspec(dllexport) uintptr_t g_rsxr_fn_msort, g_rsxr_fn_mins1, g_rsxr_fn_mins2, g_rsxr_fn_mcmp,
    g_rsxr_matcher_table;
// The port over n targets with the bound game addresses, no SEH frame (emulator tests compare it with
// the stock 0x4E7B30).
extern "C" __declspec(dllexport) int __cdecl RsxrNs_MatchPort(const uint8_t* matcher, const int32_t* in, int n, int tol,
                                                              int hist);
