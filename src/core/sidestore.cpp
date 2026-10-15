#include "core/sidestore.h"
#include <windows.h>
#include <unordered_map>

namespace {
SRWLOCK g_lock = SRWLOCK_INIT;

std::unordered_map<uintptr_t, Rs7Entry>& Map() {
    static std::unordered_map<uintptr_t, Rs7Entry> m;

    return m;
}

struct ExclusiveGuard {
    ExclusiveGuard() { AcquireSRWLockExclusive(&g_lock); }

    ~ExclusiveGuard() { ReleaseSRWLockExclusive(&g_lock); }
};

struct SharedGuard {
    SharedGuard() { AcquireSRWLockShared(&g_lock); }

    ~SharedGuard() { ReleaseSRWLockShared(&g_lock); }
};
}  // namespace

// No exception may leave these: they are called from naked asm stubs inside game frames.
void Rs7Store_Set(const void* key, Rs7Entry e) {
    ExclusiveGuard guard;

    try {
        // An inactive entry reads exactly like a missing one (Get callers test .active), so it is not
        // kept: one entry per song and object would otherwise stay for the life of the game (leak
        // audit). Active entries are capped: a new song's objects are new addresses, the older ones
        // are dead sessions (a live session registers just before it plays).
        if (!e.active) {
            Map().erase(reinterpret_cast<uintptr_t>(key));
            return;
        }

        if (Map().size() >= kRs7StoreMax && Map().find(reinterpret_cast<uintptr_t>(key)) == Map().end()) {
            Map().clear();
        }
        Map()[reinterpret_cast<uintptr_t>(key)] = e;
    } catch (...) {
        // Out of memory: drop the entry; callers then see "not active" (stock behaviour).
    }
}

bool Rs7Store_Get(const void* key, Rs7Entry* out) {
    SharedGuard guard;
    const auto it = Map().find(reinterpret_cast<uintptr_t>(key));
    if (it == Map().end()) {
        return false;
    }

    *out = it->second;

    return true;
}

bool Rs7Store_AnyActive() {
    SharedGuard guard;

    for (const auto& kv : Map()) {
        if (kv.second.active) {
            return true;
        }
    }

    return false;
}

void Rs7Store_ClearAll() {
    ExclusiveGuard guard;

    Map().clear();
}

int Rs7Store_Count() {
    SharedGuard guard;

    return int(Map().size());
}
