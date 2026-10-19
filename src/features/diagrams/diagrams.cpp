// Chord-diagram logic: the LowB / LowFs keys of a seven/eight-string shape (T8 record or manifest arrays).
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include <algorithm>

namespace rs7diagram {
bool FromT8(const uint8_t* t, Shape* out) {
    if (!t || !out || t[rs7ns::kT8Slots] < 7 || t[rs7ns::kT8Slots] > 8) {
        return false;
    }

    out->count = t[rs7ns::kT8Slots];
    out->manifest = false;
    out->capo = 0;

    for (int i = 0; i < 8; ++i) {
        out->frets[i] = static_cast<int8_t>(t[rs7ns::kT8Frets + i]);
        out->fingers[i] = static_cast<int8_t>(t[rs7ns::kT8Fingers + i]);
    }

    return true;
}

bool MergeManifestT8(const uint8_t* t, Shape* shape) {
    Shape loaded;
    if (!shape || !FromT8(t, &loaded)) {
        return false;
    }

    for (int i = 0; i < 6; ++i) {
        int f = shape->frets[i] == shape->capo ? 0 : shape->frets[i];
        int finger = shape->fingers[i] == 255 ? -1 : shape->fingers[i];
        if (f != loaded.frets[i] || finger != loaded.fingers[i]) {
            return false;
        }
    }

    shape->count = loaded.count;

    for (int i = 6; i < loaded.count; ++i) {
        // T8 already applies the capo=open rule. Convert open back to capo for
        // the manifest normalizer's scan, so it doesn't create a false minimum.
        shape->frets[i] = loaded.frets[i] == 0 ? shape->capo : loaded.frets[i];
        shape->fingers[i] = loaded.fingers[i];
    }

    return true;
}

void Emit(const Shape& s, Put put, void* ctx) {
    if (s.count < 7 || s.count > 8 || !put) {
        return;
    }

    int lo = 255, hi = -1;

    for (int i = 0; i < s.count; ++i) {
        int f = s.frets[i];

        // Use the same open-string normalization for the window and its rows.
        // A capo-only shape has no fretted minimum, even when capo >= 4.
        if (s.manifest && f == s.capo) {
            f = 0;
        }

        if (f > 0) {
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
    }

    int start = hi < 4 ? 1 : lo, offset = hi < 4 ? 0 : lo - 1;

    put(ctx, "StartFret", start);
    const char* const keys[] = {"LowEFret", "AFret", "DFret", "GFret", "BFret", "HighEFret", "LowBFret", "LowFsFret"};

    for (int i = 0; i < s.count; ++i) {
        int f = s.frets[i];

        if (s.manifest && f == s.capo) {
            f = 0;
        }
        if (f > 0) {
            f -= offset;
        }
        put(ctx, keys[i], f);

        if (i >= 6) {
            const char* finger = i == 6 ? "LowB_Finger" : "LowFs_Finger";

            if (s.frets[i] > s.capo && s.fingers[i] >= 0 && s.fingers[i] <= 4) {
                put(ctx, finger, s.fingers[i]);
            }
            put(ctx, i == 6 ? "LowB_Color" : "LowFs_Color", i == 6 ? 0x808080 : 0x00ffff);
        }
    }
}
}  // namespace rs7diagram
