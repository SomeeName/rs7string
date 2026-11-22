// The song's detector width: the string count of a validated tuning and its lowest open note (logic).
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "model/slots.h"

unsigned rsxrpubstate::Count(size_t n, bool bass) {
    return bass ? rsxr::kStockBassStrings : rsxr::GuitarStrings(n);
}

int rsxrpubstate::Lowest(const int16_t t[8], unsigned n, bool bass) {
    const int* base = bass ? g_rsxr_pitch_bbase : g_rsxr_pitch_gbase;
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
