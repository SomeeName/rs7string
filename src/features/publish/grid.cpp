// The grid's pure parts: physical string ranks, finger span, native cell positions and GridBuild's wait table.
#include "features/fretgrid/fretgrid.h"
#include "features/publish/publish.h"
#include <algorithm>
#include <cstring>

namespace rsxrpubgrid {
unsigned PhysicalRank(unsigned s, unsigned n) {
    if (n <= 6) {
        return s;
    }

    return s < 6 ? s + n - 6 : n - 1 - s;
}

unsigned PhysicalEdge(unsigned n, bool first, bool bass) {
    if (bass && n >= 4 && n <= 6) {
        return first ? (n > 4 ? 4u : 0u) : (n == 6 ? 5u : 3u);
    }

    return first && n > 6 ? n - 1 : first ? 0 : n > 6 ? 5 : n - 1;
}

unsigned FingerRank(unsigned s, unsigned n, bool bass) {
    // Bass keeps six template slots: E A D G in 0-3, the low B in 4 (lowest), the high C in 5 (highest).
    if (bass) {
        return s < 4 ? s + 1 : s == 4 ? 0 : s;
    }

    return PhysicalRank(s, n);
}

int FingerSpan(const uint8_t* fingers, unsigned n, unsigned finger, int* span, bool bass) {
    // Stock's no-match result is 0-6+1=-5 and its return tests maxIndex != 0.
    unsigned lo = n, hi = 0;

    for (unsigned s = 0; s < n; ++s) {
        if (fingers[s] == finger) {
            const unsigned r = FingerRank(s, n, bass);

            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
    }

    *span = int(hi) - int(lo) + 1;

    return hi != 0;
}

unsigned FingerAnchor(const uint8_t* fingers, unsigned n, unsigned s, bool bass) {
    // The marker loop anchors a finger on the first string in slot order, and the barre runs up from there. The low
    // strings (guitar 6/7, bass 4) are not first in slot order, so a barre that includes them must start at its
    // lowest physical string.
    if (s >= n || fingers[s] == 0xFF) {
        return s;
    }

    unsigned anchor = s;

    for (unsigned i = 0; i < n; ++i) {
        if (fingers[i] == fingers[s] && FingerRank(i, n, bass) < FingerRank(anchor, n, bass)) {
            anchor = i;
        }
    }

    return anchor;
}

// The marker update 0x58C690 seeks the barre clip to clamp((span - 1) / 5, 0, 1) of its length, and the clip ends at
// five gaps of clipGap. The barre needs span - 1 gaps of gap: up to the clip's end that is a seek (scale the duration
// the seek multiplies), beyond it the clip stays at its end and the capsule is scaled up.
namespace {
float BarreReach(int span, float gap, float clipGap) {
    return span <= 1 || !(gap > 0.0f) || !(clipGap > 0.0f) ? 0.0f : float(span - 1) * gap / (5.0f * clipGap);
}
}  // namespace

float BarreClipScale(int span, float gap, float clipGap) {
    const float reach = BarreReach(span, gap, clipGap);

    if (!(reach > 0.0f)) {
        return 1.0f;
    }

    return std::min(reach, 1.0f) / std::min(float(span - 1) / 5.0f, 1.0f);
}

float BarreEntityScale(int span, float gap, float clipGap) {
    return std::max(BarreReach(span, gap, clipGap), 1.0f);
}

int GetCellPos(const uint8_t* grid, unsigned n, unsigned fret, unsigned k, unsigned s, float* out) {
    if (!grid || !out || n > 8 || !n || fret < 1 || fret > 26 || k > 2 || s >= n) {
        return 0;
    }

    const auto* row = grid + 4 + (fret - 1) * rsxrfg::kRowNative;
    const auto* cell = row + 4 + s * rsxrfg::kCell;

    std::memcpy(out, cell + rsxrfg::kCellWorld + k * 12, 12);

    return 1;
}
}  // namespace rsxrpubgrid

namespace rsxrpubgrid {
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
}  // namespace rsxrpubgrid
