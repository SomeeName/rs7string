#include "core/rampstate.h"
#include "core/colournative.h"
#include "core/ramp.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cstring>

namespace {
Rs7Cand g_cands[kRs7MaxCands];
volatile LONG g_count = 0;
SRWLOCK g_addLock = SRWLOCK_INIT;  // serialises writers of the table (Add, Claim, Retire, ResetForTests)
SRWLOCK g_v6Lock = SRWLOCK_INIT;   // serialises UpdateV6 (rule + store + log as one step)
volatile LONG g_swaps = 0, g_hidden = 0, g_faults = 0;

LONG Bits(float f) {
    LONG u;

    std::memcpy(&u, &f, sizeof u);

    return u;
}

bool V6IsOn() {
    return g_rs7_v6[0] != 0.0f;
}

void StoreV6(bool on) {
    for (int m = 0; m < 2; ++m) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_rs7_v6[m]), on ? Bits(kRs7V6[m]) : 0);
    }

    // Review #5: callers include the ramp worker (our own thread). Never touch the colour manager here: mark the
    // palette pending; the game thread publishes it (CMBUILD / FGHK / the SetTexture hook, colournative.h).
    rs7colour::RequestPublish();
}
}  // namespace

int Rs7Cand_Add(void* d3d, Rs7Family f, const char* path, uintptr_t obj) {
    if (!d3d) {
        return -1;
    }

    AcquireSRWLockExclusive(&g_addLock);
    const LONG n = g_count;
    int idx = -1, freeSlot = -1;

    for (LONG i = 0; i < n; ++i) {
        if (g_cands[i].d3d == d3d) {
            idx = int(i);
            break;
        }

        if (freeSlot < 0 && g_cands[i].state == Rs7CandState::Retired) {
            freeSlot = int(i);
        }
    }

    if (idx < 0 && (freeSlot >= 0 || n < kRs7MaxCands)) {
        const int slot = freeSlot >= 0 ? freeSlot : int(n);
        Rs7Cand& c = g_cands[slot];

        c.d3d = nullptr;  // a reused slot: unpublished while it is rewritten
        c.family = f;
        c.state = Rs7CandState::Pending;
        c.stock = nullptr;
        c.ours = nullptr;
        c.hash = 0;
        c.retries = 0;
        c.retryAtMs = 0;
        c.obj = obj;
        c.gone = 0;
        size_t len = 0;

        if (path) {
            for (; path[len] && len < sizeof c.path - 1; ++len) {
                c.path[len] = path[len];
            }
        }

        c.path[len] = '\0';
        // Publish (full barriers: the fields are visible before the pointer / the count).
        InterlockedExchangePointer(const_cast<void**>(&c.d3d), d3d);
        if (slot == int(n)) {
            InterlockedExchange(&g_count, n + 1);
        }
        idx = slot;
    }

    ReleaseSRWLockExclusive(&g_addLock);

    return idx;
}

Rs7Cand* Rs7Cand_Find(const void* d3d) {
    if (!d3d) {
        return nullptr;
    }

    const LONG n = g_count;

    for (LONG i = 0; i < n; ++i) {
        if (g_cands[i].d3d == d3d) {
            return &g_cands[i];
        }
    }

    return nullptr;
}

int Rs7Cand_Count() {
    return int(g_count);
}

Rs7Cand* Rs7Cand_At(int i) {
    return i >= 0 && i < int(g_count) ? &g_cands[i] : nullptr;
}

bool Rs7Cand_Claim(Rs7Cand* c, const void* d3d) {
    if (!c || !d3d) {
        return false;
    }

    AcquireSRWLockExclusive(&g_addLock);
    const bool ok = c->d3d == d3d && (c->state == Rs7CandState::Pending || c->state == Rs7CandState::Retry);

    if (ok) {
        c->state = Rs7CandState::Busy;
    }
    ReleaseSRWLockExclusive(&g_addLock);

    return ok;
}

bool Rs7Cand_Retire(Rs7Cand* c, const void* d3d, bool byOwner) {
    if (!c || !d3d) {
        return false;
    }

    AcquireSRWLockExclusive(&g_addLock);
    const Rs7CandState s = c->state;
    const bool ok = c->d3d == d3d && s != Rs7CandState::Retired &&
                    (byOwner || (s != Rs7CandState::Active && s != Rs7CandState::Busy));

    if (ok) {
        InterlockedExchangePointer(const_cast<void**>(&c->d3d), nullptr);  // Find stops matching first
        c->state = Rs7CandState::Retired;
        c->stock = nullptr;
        c->ours = nullptr;
        c->gone = 0;
        c->obj = 0;
    }

    ReleaseSRWLockExclusive(&g_addLock);

    return ok;
}

bool Rs7Ramp_V6Rule(bool enabled, bool deviceOk) {
    if (!enabled || !deviceOk) {
        return false;
    }

    bool noteActive = false;
    const LONG n = g_count;

    for (LONG i = 0; i < n; ++i) {
        const Rs7CandState s = g_cands[i].state;
        if (s == Rs7CandState::Refused || s == Rs7CandState::Retry) {
            return false;
        }

        if (s == Rs7CandState::Active && g_cands[i].family == Rs7Family::Note) {
            noteActive = true;
        }
    }

    return noteActive;
}

void Rs7Ramp_UpdateV6(bool enabled, bool deviceOk, const char* reason) {
    AcquireSRWLockExclusive(&g_v6Lock);
    const bool on = Rs7Ramp_V6Rule(enabled, deviceOk);

    if (on != V6IsOn()) {
        StoreV6(on);

        try {  // in order with the store; never leave the lock held
            Rs7Log("ramp: V6 %s (%s) tid %lu", on ? "ON" : "OFF", reason ? reason : "", GetCurrentThreadId());
        } catch (...) {
        }
    }

    ReleaseSRWLockExclusive(&g_v6Lock);
}

void Rs7Ramp_CountSwap() {
    InterlockedIncrement(&g_swaps);
}

void Rs7Ramp_CountHidden() {
    InterlockedIncrement(&g_hidden);
}

void Rs7Ramp_CountFault() {
    InterlockedIncrement(&g_faults);
}

void Rs7Ramp_LogStats() {
    unsigned st[6] = {0, 0, 0, 0, 0, 0};  // indexed by Rs7CandState
    const LONG n = g_count;

    for (LONG i = 0; i < n; ++i) {
        const unsigned s = unsigned(g_cands[i].state);

        if (s < 6) {
            ++st[s];
        }
    }

    st[unsigned(Rs7CandState::Pending)] += st[unsigned(Rs7CandState::Busy)];
    const unsigned live = unsigned(n) - st[unsigned(Rs7CandState::Retired)];

    Rs7Log("ramp stats: swaps=%u hidden=%u faults=%u candidates %u (active %u, refused %u, retry %u, pending %u) v6=%s",
           unsigned(g_swaps), unsigned(g_hidden), unsigned(g_faults), live, st[unsigned(Rs7CandState::Active)],
           st[unsigned(Rs7CandState::Refused)], st[unsigned(Rs7CandState::Retry)], st[unsigned(Rs7CandState::Pending)],
           V6IsOn() ? "ON" : "OFF");
}

void Rs7Cand_ResetForTests() {
    AcquireSRWLockExclusive(&g_addLock);
    InterlockedExchange(&g_count, 0);
    std::memset(g_cands, 0, sizeof g_cands);
    ReleaseSRWLockExclusive(&g_addLock);
    InterlockedExchange(&g_swaps, 0);
    InterlockedExchange(&g_hidden, 0);
    InterlockedExchange(&g_faults, 0);
    StoreV6(false);
}
