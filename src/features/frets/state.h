#pragma once
#include "features/frets/frets.h"
#include "model/objtable.h"

namespace rs7fretglue {
// Per Rnd_Guitar. A full table replaces its entries in turn.
struct GuitarState {
    bool applied;    // bars re-placed and scaled (by F1 or F2) and not restored since
    int floorMoved;  // spacings moved by this transform; -1 = floor not ready yet
    float halfLen;   // the fret_halflen of that apply (overlay: a change re-applies)
    int verify;      // N5 frames until the node read-back (0 = none pending)
};

struct GlowBar {
    const uint8_t* obj;
    float k;
};

struct State {
    // spin guards the guitar table for hook callbacks and overlay statistics.
    rs7::ObjTable<const void*, GuitarState, rs7frets::kMaxGuitars> guitars;
    volatile long spin = 0;

    // Hook callbacks and song setup access log gates through interlocked operations; no table lock.
    volatile long logApply = 0, logF1 = 0, logVerify = 0, logFail = 0;
    volatile long logFloor = 0, logInlay = 0;
    volatile long logGlow = 0, logGlowBar = 0;

    // Game-thread FRG1/FRG2/FRG3 and song setup own the pending glow pieces and replacement cursor; no lock. Five per
    // glow (box and four bars); a few glows can be pending at once.
    GlowBar glowBars[20] = {};
    int glowNext = 0;
};

extern State g_state;
}  // namespace rs7fretglue
