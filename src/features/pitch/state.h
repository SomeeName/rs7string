#pragma once
#include "features/bass/bass.h"
#include "features/pitch/pitch.h"
#include "model/objtable.h"
#include <windows.h>
#include <atomic>
#include <optional>
#include <unordered_map>

namespace rs7p {
struct State {
    // Cold writers hold tailedLock for keys, ages, clock and eviction logging; hot readers acquire only the keys.
    std::atomic<uintptr_t> tailed[kTailedMax]{};
    uint32_t tailedAge[kTailedMax]{};
    uint32_t tailedClock = 0;
    long tailedLock = 0;
    bool tailedEvictLogged = false;

    // Game-thread tuning/reset callbacks own these tables; no lock.
    // Kept 7/8-entry song tuning counts, detector widths (cleared by PR0/next S4) and recorded reset floors.
    rs7::ObjTable<const uint8_t*, size_t, rs7::kMaxGuitars> song;
    rs7::ObjTable<const uint8_t*, unsigned, rs7::kMaxGuitars> songCount;
    rs7::ObjTable<const uint8_t*, int, rs7::kMaxGuitars> floor;

    // Game-thread PBH and song setup own channel 0/2 bend holds and the first-40-notes log budget; no lock.
    rs7bass::BendHold bendHold[2];
    long bendLogged = 0;

    // storeLock guards the side store (exclusive writers, shared readers). Map() retains first-use construction.
    SRWLOCK storeLock = SRWLOCK_INIT;
    std::optional<std::unordered_map<uintptr_t, Rs7Entry>> store;
};

extern State g_state;
}  // namespace rs7p
