// Colour channels: float channels, recolouring a ramp line (the 0xRRGGBB text parser is platform/config.h).
#include "features/colours/colours.h"

void Rs7_RgbToFloats(uint32_t rgb, float out[3]) {
    out[0] = float((rgb >> 16) & 0xFF) / 255.0f;
    out[1] = float((rgb >> 8) & 0xFF) / 255.0f;
    out[2] = float(rgb & 0xFF) / 255.0f;
}

uint8_t Rs7_Recolor(uint8_t g, uint8_t a, uint8_t c) {
    if (a == 0) {
        return g == 0 ? 0 : uint8_t(c + ((255 - c) * g + 127) / 255);
    }
    if (g <= a) {
        return uint8_t((c * g + a / 2) / a);
    }
    if (a == 255) {
        return c;
    }

    return uint8_t(c + ((255 - c) * (g - a) + (255 - a) / 2) / (255 - a));
}
