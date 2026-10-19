// The song's detector width: the string count of a validated tuning and its lowest open note (logic).
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"

unsigned rs7pubstate::Count(size_t n, bool bass) {
    return bass ? 4 : n == 7 || n == 8 ? unsigned(n) : 6;
}

int rs7pubstate::Lowest(const int16_t t[8], unsigned n, bool bass) {
    const int* base = bass ? g_rs7_pitch_bbase : g_rs7_pitch_gbase;
    const unsigned limit = bass ? 4 : 8;

    if (!n || n > limit) {
        n = bass ? 4 : 6;
    }
    int low = base[0] + t[0];

    for (unsigned s = 1; s < n; ++s) {
        const int pitch = base[s] + t[s];

        if (pitch < low) {
            low = pitch;
        }
    }

    return low;
}
