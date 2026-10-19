#pragma once
#include "features/nslot/nslot.h"
#include <cstdint>

// Native N-slot chord matcher (issue #18 stage 2). Play_Matcher_DetectChord 0x4E7B30 (stdcall
// (matcher, int32 targets[], tolerance, history index), AL = matched) takes eight targets: every caller
// hands it an int32[8] (strings 0-7, -1 = none; nslot.h). MD1 (its entry) calls
// Rs7Ns_DetectChord:
//  - at most six non-negative targets: they are compacted (order kept, the -1 holes dropped) into
//    out6 and the stock body runs on them (returns -1). Dropping holes changes nothing the stock
//    algorithm looks at (dedupe, active count, lowest target, subharmonic rows, OctaveDyad), so these
//    chords are judged bit for bit as before;
//  - seven or eight: Rs7Ns_MatchN, the stock algorithm over n targets (returns 0/1).
// docs/ARCHITECTURE.md has the port's derivation.
extern "C" __declspec(dllexport) int __cdecl Rs7Ns_DetectChord(const uint8_t* matcher, const int32_t* in8, int tol,
                                                               int hist, int32_t* out6);

namespace rs7match {
constexpr int kMaxTargets = 8;

// Addresses bound by Rs7_Startup from sites.json (X_MSORT, X_MINS1, X_MINS2: the std::sort pieces the
// stock matcher calls; X_MCMP: its comparator; X_MTAB: the subharmonic table 0x119A398).
struct Game {
    uintptr_t sort = 0, ins1 = 0, ins2 = 0, cmp = 0, table = 0;
    int misses = 2;  // seven/eight targets: how many may be missing ([chords] wide_misses)
};

// The stock algorithm over n (<= 8) targets. Pure apart from the game memory it reads (matcher,
// detector history frame, subharmonic table) and the game's sort routines.
int MatchN(const uint8_t* matcher, const int32_t* in, int n, int tol, int hist, const Game& g);
// 0x4E8650: exactly two positive targets and the second is the first + 12 (an octave dyad).
bool OctaveDyad(const int32_t* in, int n);
// Compacts the non-negative entries of in[0..n) into out6; false if there are more than six.
bool Compact6(const int32_t* in, int n, int32_t* out6);
}  // namespace rs7match

extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_msort, g_rs7_fn_mins1, g_rs7_fn_mins2, g_rs7_fn_mcmp,
    g_rs7_matcher_table;
// The port over n targets with the bound game addresses, no SEH frame (emulator tests compare it with
// the stock 0x4E7B30).
extern "C" __declspec(dllexport) int __cdecl Rs7Ns_MatchPort(const uint8_t* matcher, const int32_t* in, int n, int tol,
                                                             int hist);
