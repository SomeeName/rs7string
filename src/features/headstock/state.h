#pragma once
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_head.h"
#include "features/headstock/pegset.h"
#include <windows.h>
#include <cstdint>

// Headstock runtime state: one instance (g_head, hooks.cpp). Asset settings retain their startup construction
// order in ConfigState (assetload_config.h / assetload_config.cpp).
namespace rs7head {
// [debug] head_cull as the overlay hands it over (Rs7Head_SetLive, any thread).
struct Live {
    char cull[48];
};

struct Width {
    uint8_t* guitar;
    uint8_t* root;
    float stock[9];
    float applied;
};

struct Culled {
    uint8_t* head;
    uint8_t* root;
    uint8_t* node;
};

struct State {
    // Counters since startup (PSC1 / PSL2 run on resource-loader jobs: interlocked).
    volatile long ctors, loads7, active7, foreign, ownFull;
    // Game / loader thread log limits: interlocked by Rs7Log_Upto, reset at song setup.
    volatile long logOwnFull, logCtor, logLoad, logRaise, logNoSlot, logForeign;
    // Loader threads publish; game thread consumes. LoadRing guards records with its per-entry spin locks.
    LoadRing loads;

    // Startup sets ini before hooks; the game thread owns cullName / reloadFrames (no lock).
    wchar_t ini[MAX_PATH];
    char cullName[48];
    int reloadFrames;
    // Any thread publishes live under liveSpin; liveSeq / liveOwned use interlocked publication.
    Live live;
    volatile long liveSpin, liveSeq, liveOwned;
    // Game thread's last consumed liveSeq (no lock).
    long liveTaken;

    // Game thread: per-guitar model, width and diagnostic culling caches (no lock).
    rs7asset::Head heads[8];
    Width width[rs7::kMaxGuitars];
    Culled culled[4];
    unsigned culledNext;
    const uint8_t* lastLetterInfo;
    int lastLetterResult = -1;
    unsigned colourLogs;
    // Game thread; the peg-swap flag retains its interlocked write and volatile reads.
    volatile long pegSwapEver;
    // Game thread log limits, retaining interlocked Rs7Log_Upto updates.
    volatile long logFrameCheck, logNoAlt, logFrame, logLetters;
    volatile long logPegs, logPegAlt, logBind, logSeg7, logLayer, logWidth;

    // Startup writes before hooks; loader threads then read without a lock.
    // selfbuild=1 and no usable model: build from the stock assets on the first frame load, once per hand.
    bool sbWanted, sbWantedLh;
    bool sbBass;                          // selfbuild=1 and headstock7 on: a bass frame without models builds them
    bool sbBassStale, sbBassStaleLh;      // bass models built from an older package
    float wood = rs7asset::kDefaultWood;  // model wood colour texture brightness
    // Startup initializes; loader fallback writes interlocked, PSL1 reads the volatile per-hand entry.
    // Locator_Peg_1..n capacity: [0] right, [1] left, 7 when the 8-peg model falls back to the 7-peg one.
    volatile long modelPegs[2] = {7, 7};
    // Loader threads claim / publish per-hand guitar builds (and ConfigState::file7 / file7Lh) interlocked.
    volatile long sbState, sbStateLh;  // 0 untried, 1 running, 2 succeeded, 3 failed

    // Resource-loader threads: interlocked log limits (no additional lock).
    volatile long logSeen, logMerged, logMissing, logFail, logLayout, logTexCheck, logTex;
    // Resource-loader threads claim / publish shared and bass builds through interlocked operations.
    volatile long sbShared, sbBassRh, sbBassLh;
    // Resource-loader callbacks only take these stable addresses; the tags are never written.
    int bassLhTag = 1, guitarLhTag = 1;
    // Resource-loader threads: temporary-file sequence incremented interlocked.
    volatile long buildSequence;
    // Resource-loader threads: shared DDS fault snapshot, unguarded as before; ddsDead is set interlocked.
    DWORD ddsFaultCode;
    uintptr_t ddsFaultAt;
    // First handler fault skips AdapterDtor and may leave a game lock held: never call the handler again.
    volatile long ddsDead;
};

extern State g_head;
}  // namespace rs7head
