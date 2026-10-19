#include "model/stringcolour.h"

void Rs7S7_DimColour(const float rgb[3], float dimValue, float out[3]) {
    const float m = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
    const float k = m > 0.0f ? dimValue / m : 0.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = rgb[i] * k;
    }
}

void Rs7S7_GlowColour(const float rgb[3], float out[3]) {
    const float m = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
    const float k = m > 0.0f ? 1.0f / m : 0.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = rgb[i] * k;
    }
}
