#pragma once
#include "features/headstock/pegset.h"
#include <windows.h>
#include <cstdint>

// The state of the PegSet glue (hooks_pegset.cpp, hooks_debug.cpp): one struct, one instance (g_head, hooks.cpp).
// Game thread unless a member says otherwise. The 7-string model keeps its own (assetload_config.h,
// assetload_head.cpp).
namespace rs7head {
// [debug] head_cull / head_stretch / head_stretch_mode as the overlay hands them over (Rs7Head_SetLive, any thread).
struct Live {
    char cull[48];
    float st;
    int mode;
};

struct State {
    // Counters since startup (PSC1 / PSL2 run on resource-loader jobs: interlocked).
    volatile long ctors, loads7, active7, foreign, ownFull;
    // Log once / a few times.
    volatile long logOwnFull, logCtor, logLoad, logRaise, logNoSlot, logForeign, logStretch;
    // Loads PSL2 completed, for N5 to log (PSL2 runs inside the guitar's resource-loader job: no logging there).
    LoadRing loads;

    // The debug experiments (hooks_debug.cpp): the ini they are re-read from, what is applied.
    wchar_t ini[MAX_PATH];
    char cullName[48];
    float stretch = 1.0f, stretchApplied = 1.0f;
    int stretchMode;
    uint8_t* culled;
    int reloadFrames;
    // The overlay's values: a spin lock around `live`, published by liveSeq; liveOwned = the ini is no longer read.
    Live live;
    volatile long liveSpin, liveSeq, liveOwned;
    long liveTaken;
};

extern State g_head;
}  // namespace rs7head
