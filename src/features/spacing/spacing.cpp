// The spacing factor each instrument was built with (Rs7Gs_AttachSpaced records it, the headstock reads it).
#include "features/spacing/spacing.h"
#include <windows.h>

namespace {
constexpr int kSlots = 8;

struct Entry {
    const uint8_t* info;
    float k;
};

Entry g_entries[kSlots] = {};
unsigned g_next = 0;
SRWLOCK g_lock = SRWLOCK_INIT;
}  // namespace

void Rs7Spacing_Record(const uint8_t* info, float k) {
    if (!info) {
        return;
    }

    AcquireSRWLockExclusive(&g_lock);

    for (Entry& e : g_entries) {
        if (e.info == info) {
            e.k = k;
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
    }

    Entry& e = g_entries[g_next++ % kSlots];

    e.info = nullptr;
    e.k = k;
    e.info = info;
    ReleaseSRWLockExclusive(&g_lock);
}

void Rs7Spacing_Forget(const uint8_t* info) {
    if (!info) {
        return;
    }

    AcquireSRWLockExclusive(&g_lock);

    for (Entry& e : g_entries) {
        if (e.info == info) {
            e.k = 1.0f;
        }
    }

    ReleaseSRWLockExclusive(&g_lock);
}

float Rs7Spacing_Factor(const uint8_t* info) {
    if (!info) {
        return 1.0f;
    }

    float k = 1.0f;

    AcquireSRWLockShared(&g_lock);

    for (const Entry& e : g_entries) {
        if (e.info == info) {
            k = e.k;
            break;
        }
    }

    ReleaseSRWLockShared(&g_lock);

    return k;
}
