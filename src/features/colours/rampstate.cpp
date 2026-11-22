// The ramp candidate table (stock ramp textures found in the engine's texture list) and the V6 rule.
#include "features/colours/state.h"
#include "model/groups.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

namespace {
using rsxrcolour::g_colour;

LONG Bits(float f) {
    LONG u;

    std::memcpy(&u, &f, sizeof u);

    return u;
}

bool V6IsOn() {
    return g_rsxr_v6[0] != 0.0f;
}

void StoreV6(bool on) {
    for (int m = 0; m < 2; ++m) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_rsxr_v6[m]), on ? Bits(kRsxrV6[m]) : 0);
    }

    // Callers include the ramp worker (our own thread). Never touch the colour manager here: mark the palette
    // pending; the game thread publishes it (CMBUILD / FGHK / the SetTexture hook, hooks_publish.cpp).
    rsxrcolour::RequestPublish();
}
}  // namespace

int RsxrCand_Add(void* d3d, RsxrFamily f, const char* path, uintptr_t obj) {
    if (!d3d) {
        return -1;
    }

    // The game's file name, copied before the lock: a fault reading it (caught by RsxrMid_Guard further up) must
    // not leave addLock held.
    char name[sizeof g_colour.cands[0].path] = {};

    for (size_t i = 0; path && path[i] && i < sizeof name - 1; ++i) {
        name[i] = path[i];
    }

    AcquireSRWLockExclusive(&g_colour.addLock);
    const LONG n = g_colour.candCount;
    int idx = -1, freeSlot = -1;

    for (LONG i = 0; i < n; ++i) {
        if (g_colour.cands[i].d3d == d3d) {
            idx = int(i);
            break;
        }

        if (freeSlot < 0 && g_colour.cands[i].state == RsxrCandState::Retired) {
            freeSlot = int(i);
        }
    }

    if (idx < 0 && (freeSlot >= 0 || n < kRsxrMaxCands)) {
        const int slot = freeSlot >= 0 ? freeSlot : int(n);
        RsxrCand& c = g_colour.cands[slot];

        c.d3d = nullptr;  // a reused slot: unpublished while it is rewritten
        c.family = f;
        c.state = RsxrCandState::Pending;
        c.stock = nullptr;
        c.ours = nullptr;
        c.hash = 0;
        c.colourGeneration = 0;
        c.retries = 0;
        c.retryAtMs = 0;
        c.obj = obj;
        c.gone = 0;
        std::memcpy(c.path, name, sizeof c.path);
        // Publish (full barriers: the fields are visible before the pointer / the count).
        InterlockedExchangePointer(const_cast<void**>(&c.d3d), d3d);
        if (slot == int(n)) {
            InterlockedExchange(&g_colour.candCount, n + 1);
        }
        idx = slot;
    }

    ReleaseSRWLockExclusive(&g_colour.addLock);

    return idx;
}

RsxrCand* RsxrCand_Find(const void* d3d) {
    if (!d3d) {
        return nullptr;
    }

    const LONG n = g_colour.candCount;

    for (LONG i = 0; i < n; ++i) {
        if (g_colour.cands[i].d3d == d3d) {
            return &g_colour.cands[i];
        }
    }

    return nullptr;
}

int RsxrCand_Count() {
    return int(g_colour.candCount);
}

RsxrCand* RsxrCand_At(int i) {
    return i >= 0 && i < int(g_colour.candCount) ? &g_colour.cands[i] : nullptr;
}

bool RsxrCand_Claim(RsxrCand* c, const void* d3d) {
    if (!c || !d3d) {
        return false;
    }

    AcquireSRWLockExclusive(&g_colour.addLock);
    const bool ok = c->d3d == d3d && (c->state == RsxrCandState::Pending || c->state == RsxrCandState::Retry);

    if (ok) {
        c->state = RsxrCandState::Busy;
    }
    ReleaseSRWLockExclusive(&g_colour.addLock);

    return ok;
}

bool RsxrCand_Retire(RsxrCand* c, const void* d3d, bool byOwner) {
    if (!c || !d3d) {
        return false;
    }

    AcquireSRWLockExclusive(&g_colour.addLock);
    const RsxrCandState s = c->state;
    const bool ok = c->d3d == d3d && s != RsxrCandState::Retired &&
                    (byOwner || (s != RsxrCandState::Active && s != RsxrCandState::Busy));

    if (ok) {
        InterlockedExchangePointer(const_cast<void**>(&c->d3d), nullptr);  // Find stops matching first
        c->state = RsxrCandState::Retired;
        c->stock = nullptr;
        c->ours = nullptr;
        c->gone = 0;
        c->obj = 0;
    }

    ReleaseSRWLockExclusive(&g_colour.addLock);

    return ok;
}

bool RsxrRamp_V6Rule(bool enabled, bool deviceOk) {
    if (!enabled || !deviceOk) {
        return false;
    }

    bool noteActive = false;
    const LONG n = g_colour.candCount;

    for (LONG i = 0; i < n; ++i) {
        const RsxrCandState s = g_colour.cands[i].state;
        if (g_colour.cands[i].family == RsxrFamily::Embedded) {
            continue;
        }

        if (s == RsxrCandState::Refused || s == RsxrCandState::Retry) {
            return false;
        }

        if (s == RsxrCandState::Active && g_colour.cands[i].family == RsxrFamily::Note) {
            noteActive = true;
        }
    }

    return noteActive;
}

void RsxrRamp_UpdateV6(bool enabled, bool deviceOk, const char* reason) {
    AcquireSRWLockExclusive(&g_colour.v6Lock);
    const bool on = RsxrRamp_V6Rule(enabled, deviceOk);

    if (on != V6IsOn()) {
        StoreV6(on);

        try {  // in order with the store; never leave the lock held
            RsxrLog("ramp: V6 %s (%s) tid %lu", on ? "ON" : "OFF", reason ? reason : "", GetCurrentThreadId());
        } catch (...) {
        }
    }

    ReleaseSRWLockExclusive(&g_colour.v6Lock);
}

void RsxrRamp_CountSwap() {
    InterlockedIncrement(&g_colour.swaps);
}

void RsxrRamp_CountHidden() {
    InterlockedIncrement(&g_colour.hidden);
}

void RsxrRamp_CountFault() {
    InterlockedIncrement(&g_colour.faults);
}

void RsxrRamp_LogStats() {
    constexpr unsigned kCandStates = unsigned(RsxrCandState::Retired) + 1;
    unsigned st[kCandStates] = {};  // indexed by RsxrCandState
    const LONG n = g_colour.candCount;

    for (LONG i = 0; i < n; ++i) {
        const unsigned s = unsigned(g_colour.cands[i].state);

        if (s < kCandStates) {
            ++st[s];
        }
    }

    st[unsigned(RsxrCandState::Pending)] += st[unsigned(RsxrCandState::Busy)];
    const unsigned live = unsigned(n) - st[unsigned(RsxrCandState::Retired)];

    RsxrLog(
        "ramp stats: swaps=%u hidden=%u faults=%u candidates %u (active %u, refused %u, retry %u, pending %u) v6=%s",
        unsigned(g_colour.swaps), unsigned(g_colour.hidden), unsigned(g_colour.faults), live,
        st[unsigned(RsxrCandState::Active)], st[unsigned(RsxrCandState::Refused)], st[unsigned(RsxrCandState::Retry)],
        st[unsigned(RsxrCandState::Pending)], V6IsOn() ? "ON" : "OFF");
}

void RsxrCand_ResetForTests() {
    AcquireSRWLockExclusive(&g_colour.addLock);
    InterlockedExchange(&g_colour.candCount, 0);
    std::memset(g_colour.cands, 0, sizeof g_colour.cands);
    ReleaseSRWLockExclusive(&g_colour.addLock);
    InterlockedExchange(&g_colour.swaps, 0);
    InterlockedExchange(&g_colour.hidden, 0);
    InterlockedExchange(&g_colour.faults, 0);
    StoreV6(false);
}
