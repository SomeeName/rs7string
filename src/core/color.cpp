#include "core/color.h"

namespace {
bool Blank(wchar_t ch) {
    return ch == L' ' || ch == L'\t';
}

int HexDigit(wchar_t ch) {  // ASCII only (iswxdigit would depend on the locale)
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return ch - L'a' + 10;
    }
    if (ch >= L'A' && ch <= L'F') {
        return ch - L'A' + 10;
    }

    return -1;
}
}  // namespace

bool Rs7_ParseRgb(const wchar_t* t, uint32_t* rgb) {
    if (!t) {
        return false;
    }

    while (Blank(*t)) {
        ++t;
    }

    if (*t == L'#') {
        ++t;
    }
    uint32_t v = 0;
    int n = 0;

    for (; *t && !Blank(*t); ++t, ++n) {
        const int d = HexDigit(*t);
        if (d < 0 || n >= 6) {
            return false;
        }

        v = v * 16 + static_cast<uint32_t>(d);
    }

    while (Blank(*t)) {
        ++t;
    }

    if (n != 6 || *t) {
        return false;
    }

    *rgb = v;

    return true;
}

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
