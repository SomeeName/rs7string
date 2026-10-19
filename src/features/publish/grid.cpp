// The grid's pure parts: physical string ranks, finger span, native cell positions and GridBuild's wait table.
#include "features/fretgrid/fretgrid.h"
#include "features/publish/publish.h"
#include <algorithm>
#include <cstring>

namespace rs7pubgrid {
unsigned PhysicalRank(unsigned s, unsigned n) {
    if (n <= 6) {
        return s;
    }

    return s < 6 ? s + n - 6 : n - 1 - s;
}

unsigned PhysicalEdge(unsigned n, bool first) {
    return first && n > 6 ? n - 1 : first ? 0 : n > 6 ? 5 : n - 1;
}

int FingerSpan(const uint8_t* fingers, unsigned n, unsigned finger, int* span) {
    // Stock's no-match result is 0-6+1=-5 and its return tests maxIndex != 0.
    unsigned lo = n, hi = 0;

    for (unsigned s = 0; s < n; ++s) {
        if (fingers[s] == finger) {
            const unsigned r = PhysicalRank(s, n);

            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
    }

    *span = int(hi) - int(lo) + 1;

    return hi != 0;
}

int GetCellPos(const uint8_t* grid, unsigned n, unsigned fret, unsigned k, unsigned s, float* out) {
    if (!grid || !out || n > 8 || !n || fret < 1 || fret > 26 || k > 2 || s >= n) {
        return 0;
    }

    const auto* row = grid + 4 + (fret - 1) * rs7fg::kRowNative;
    const auto* cell = row + 4 + s * rs7fg::kCell;

    std::memcpy(out, cell + rs7fg::kCellWorld + k * 12, 12);

    return 1;
}
}  // namespace rs7pubgrid

namespace rs7pubgrid {
bool WaitStep(WaitEntry* table, size_t n, const void* grid, unsigned reason, uint64_t now, uint64_t limitMs,
              const void* owner, uint64_t staleMs) {
    WaitEntry* e = nullptr;

    for (size_t i = 0; i < n && !e; ++i) {
        if (table[i].grid == grid) {
            e = &table[i];
        }
    }

    if (e && ((owner && e->owner && e->owner != owner) || (now > e->last && now - e->last > staleMs))) {
        *e = WaitEntry{};  // another build's leftovers: the deadline, the call count and a failure do not carry over
        e = nullptr;
    }

    if (!e) {  // a new grid: take a free entry, else the oldest one
        e = &table[0];

        for (size_t i = 0; i < n; ++i) {
            if (!table[i].grid) {
                e = &table[i];
                break;
            }

            if (table[i].since < e->since) {
                e = &table[i];
            }
        }

        *e = WaitEntry{grid, now, 0, 0, now, owner, false};
    }

    ++e->calls;
    e->reason = reason;
    e->last = now;

    return now - e->since >= limitMs;
}

void WaitDone(WaitEntry* table, size_t n, const void* grid, const void* owner) {
    for (size_t i = 0; i < n; ++i) {
        if (table[i].grid == grid || (owner && table[i].owner == owner)) {
            table[i] = WaitEntry{};
        }
    }
}

void MarkFailed(WaitEntry* table, size_t n, const void* grid) {
    for (size_t i = 0; i < n; ++i) {
        if (table[i].grid && table[i].grid == grid) {
            table[i].failed = true;
        }
    }
}

bool Failed(const WaitEntry* table, size_t n, const void* owner) {
    if (!owner) {
        return false;
    }

    for (size_t i = 0; i < n; ++i) {
        if (table[i].failed && table[i].owner == owner) {
            return true;
        }
    }

    return false;
}
}  // namespace rs7pubgrid
