// The ports' prerequisites and the played-note classification over the published count (logic, values in and out).
#include "features/publish/publish.h"
#include "features/pitch/pitch.h"
#include <cstring>

namespace rs7pub {
unsigned Missing(unsigned mask) {
    return kPrerequisites & ~mask;
}

bool PortsEnabled(bool requested, unsigned mask) {
    return requested && Missing(mask) == 0;
}

bool PublicationReady() {
    // Every deferred reader of the stage-5 list is ported (docs/ARCHITECTURE.md): pitch 8750C0,
    // 4E58E0, 4E6E90, 875430 (+I1 875504), 8755C0, the 1199D2C callers; FretGrid bounds, GetCellPos,
    // 58E790, 594530, edges, 58CF90; nextgrid NN1/NN3/NN4 (spawn 7A56F0 reads cells through R1 ->
    // PUBGP). Publication still needs the ports built (Rs7Publish_BuildCopies) and, per song, the
    // native owners and prepared grid (hooks_song.cpp Rs7Song_WidenDetector).
    return true;
}

int Classify(uint8_t* r, const int16_t* t, unsigned n, int midi, int initial) {
    if (!r || !t || n < 6 || n > 8) {
        return initial;
    }

    const unsigned original = r[0xC0];
    const int fret = r[0xC1];

    for (unsigned s = 0; s < n; ++s) {
        if (s == original) {
            continue;
        }

        // Unsigned arithmetic reproduces the game's 32-bit ADD/SUB, including wrap.
        const uint32_t expected = uint32_t(g_rs7_pitch_gbase[s]) + uint32_t(int(t[s])) + uint32_t(fret);
        const int32_t delta = int32_t(uint32_t(midi) - expected);
        const int32_t distance = delta < 0 ? int32_t(0u - uint32_t(delta)) : delta;

        if (distance <= 2) {  // signed abs(INT32_MIN) wraps just like CDQ/XOR/SUB in the game
            const int string = int(s);
            const int f = delta == 0 ? fret : int32_t(uint32_t(fret) + uint32_t(delta));
            const int clamped = f < 0 ? 0 : f;

            std::memcpy(r + 0x18, &string, 4);
            std::memcpy(r + 0x14, &clamped, 4);
            return delta == 0 ? 2 : 1;
        }

        if (initial != 7) {
            break;
        }
    }

    return initial;
}
}  // namespace rs7pub
