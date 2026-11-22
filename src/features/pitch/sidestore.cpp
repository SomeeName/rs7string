#include "features/pitch/state.h"
#include <windows.h>
#include <unordered_map>

namespace {
using rsxrp::g_state;

std::unordered_map<uintptr_t, RsxrEntry>& Map() {
    // Keep the map's original first-use construction and destruction order, including its local-static guard.
    struct MapLifetime {
        std::unordered_map<uintptr_t, RsxrEntry>& map = g_state.store.emplace();

        ~MapLifetime() { g_state.store.reset(); }
    };

    static MapLifetime m;

    return m.map;
}

struct ExclusiveGuard {
    ExclusiveGuard() { AcquireSRWLockExclusive(&g_state.storeLock); }

    ~ExclusiveGuard() { ReleaseSRWLockExclusive(&g_state.storeLock); }
};

struct SharedGuard {
    SharedGuard() { AcquireSRWLockShared(&g_state.storeLock); }

    ~SharedGuard() { ReleaseSRWLockShared(&g_state.storeLock); }
};
}  // namespace

// No exception may leave these: they are called from naked asm stubs inside game frames.
void RsxrStore_Set(const void* key, RsxrEntry e) {
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

        if (Map().size() >= kRsxrStoreMax && Map().find(reinterpret_cast<uintptr_t>(key)) == Map().end()) {
            Map().clear();
        }
        Map()[reinterpret_cast<uintptr_t>(key)] = e;
    } catch (...) {
        // Out of memory: drop the entry; callers then see "not active" (stock behaviour).
    }
}

bool RsxrStore_Get(const void* key, RsxrEntry* out) {
    SharedGuard guard;
    const auto it = Map().find(reinterpret_cast<uintptr_t>(key));
    if (it == Map().end()) {
        return false;
    }

    *out = it->second;

    return true;
}

bool RsxrStore_AnyActive() {
    SharedGuard guard;

    for (const auto& kv : Map()) {
        if (kv.second.active) {
            return true;
        }
    }

    return false;
}

void RsxrStore_ClearAll() {
    ExclusiveGuard guard;

    Map().clear();
}

int RsxrStore_Count() {
    SharedGuard guard;

    return int(Map().size());
}
