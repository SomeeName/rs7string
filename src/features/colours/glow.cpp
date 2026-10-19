// RSMods' glow colour conversions, float for float, and the corrected glow (colours.h).
#include "features/colours/colours.h"
#include <cmath>

namespace rs7rsmods {
bool Hue(const float rgb[3], int* hue) {
    const float r = rgb[0], g = rgb[1], b = rgb[2];
    const float lo = std::fmin(std::fmin(r, g), b), hi = std::fmax(std::fmax(r, g), b);
    if (hi == lo) {
        return false;
    }

    // RSMods CollectColors::RGB2HSL, float for float.
    const float d = hi - lo;
    float h = 0.0f;

    if (r == hi) {
        h = (g - b) / d;
    } else if (g == hi) {
        h = 2 + (b - r) / d;
    } else {
        h = 4 + (r - g) / d;
    }

    *hue = static_cast<int>(60 * h);

    if (*hue < 0) {
        *hue += 360;
    }

    return true;
}

void HslToRgb(float h, float s, float l, float out[3]) {
    // RSMods CollectColors::HSL2RGB, float for float.
    const float v =
        static_cast<float>(static_cast<double>(l) <= 0.5 ? static_cast<double>(l) * (1.0 + static_cast<double>(s))
                                                         : static_cast<double>(l) + s - static_cast<double>(l) * s);

    if (!(v > 0)) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }

    const float lo = 2 * l - v;
    const float sv = (v - lo) / v;
    const float sixth = h == 360 ? 0 : h / 60;
    const int sextant = static_cast<int>(std::floor(sixth));
    const float vsf = v * sv * (sixth - sextant);
    const float mid1 = lo + vsf, mid2 = v - vsf;
    const float table[6][3] = {{v, mid1, lo}, {mid2, v, lo}, {lo, v, mid1},
                               {lo, mid2, v}, {mid1, lo, v}, {v, lo, mid2}};

    if (sextant < 0 || sextant > 5) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        out[i] = table[sextant][i];
    }
}

bool CorrectGlow(bool custom, const float enabled[3], const float glow[3], float out[3]) {
    int hue = 0;

    for (int i = 0; i < 3; ++i) {
        out[i] = glow[i];
    }

    if (!custom || !Hue(enabled, &hue) || hue >= 4) {
        return false;
    }

    HslToRgb(static_cast<float>((hue - 4 + 360) % 360), 1.0f, 0.5f, out);

    return true;
}
}  // namespace rs7rsmods
