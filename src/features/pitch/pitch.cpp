// The native pitch storage's logic (pitch.h): the eight-entry tuning, open-MIDI and lowest-open values the tails hold.
#include "features/pitch/pitch.h"

namespace rs7p {
void FillTuning(const int16_t* v, size_t n, int16_t out[8]) {
    for (size_t i = 0; i < 8; ++i) {
        out[i] = i < n ? v[i] : int16_t(0);
    }
}

void FillOpen(const int16_t t[8], bool bass, int16_t out[8]) {
    for (int i = 0; i < 8; ++i) {
        const int base = bass ? (i < 4 ? g_rs7_pitch_bbase[i] : 0) : g_rs7_pitch_gbase[i];

        out[i] = static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint16_t>(t[i]) + base));
    }
}

int Lowest(int stock, const int16_t t[8], size_t n, bool bass) {
    if (bass) {
        return stock;
    }

    const size_t end = n < 8 ? n : 8;
    int low = stock;

    for (size_t s = 6; s < end; ++s) {
        const int v = g_rs7_pitch_gbase[s] + t[s];

        if (v < low) {
            low = v;
        }
    }

    return low;
}

uint32_t ExpectedPitch8(const int16_t tuning8[8], uint32_t instrument, uint32_t string, uint32_t fret) {
    const bool bass = instrument == 2;

    if (string >= (bass ? 4u : 8u)) {
        return fret - 1;
    }

    const int32_t base = bass ? g_rs7_pitch_bbase[string] : g_rs7_pitch_gbase[string];

    return uint32_t(int32_t(tuning8[string])) + uint32_t(base) + fret;
}
}  // namespace rs7p
