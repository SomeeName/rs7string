// The fret bars' logic (frets.h): the bar extension, the highway-floor shift, the [neck] fret_halflen parser and the
// chord-frame fit. Values in, values out; the game glue is hooks.cpp.
#include "features/frets/frets.h"
#include <cmath>
#include <cwchar>
#include <cwctype>

bool rs7frets::Compute(const float a0[3], const float a1[3], const float mid[3], float halfLen, Extend* out) {
    float d[3], len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        d[i] = a0[i] - a1[i];
        len2 += d[i] * d[i];
    }

    const float len = std::sqrt(len2);
    if (!(len > 1e-3f) || !(len < 1e3f) || !(halfLen > 0.0f)) {
        return false;
    }

    // Along u (unit, string 1 -> string 0 -> string 6), relative to string 0: the stock bar spans c -+ L.
    float c = 0.0f;

    for (int i = 0; i < 3; ++i) {
        c += (mid[i] - a0[i]) * d[i] / len;
    }

    const float m0 = c + halfLen;                         // margin beyond string 0
    const float m5 = (c - halfLen) * -1.0f - 5.0f * len;  // margin beyond string 5 (s5 at -5 len)
    const float margin = m0 > m5 ? m0 : m5;               // the wider stock margin, now beyond string 6
    const float top = len + margin, bottom = c - halfLen;
    const float along = 0.5f * (top + bottom) - c;  // centre shift along u

    out->scale = 0.5f * (top - bottom) / halfLen;

    for (int i = 0; i < 3; ++i) {
        out->shift[i] = along * d[i] / len;
    }

    out->spacing = len;

    return out->scale > 0.0f && std::isfinite(out->scale);
}

bool rs7frets::FloorShift(const float w0[3], const float w1[3], const float b[3], float out[3]) {
    float u[3], len2 = 0.0f, side = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = w0[i] - w1[i];
        len2 += u[i] * u[i];
        side += (b[i] - w0[i]) * u[i];
    }

    if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f)) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        out[i] = u[i];
    }

    return true;
}

bool rs7frets::ParseHalfLen(const wchar_t* text, float* out) {
    if (!text) {
        return false;
    }

    while (std::iswspace(*text)) {
        ++text;
    }

    if (!*text) {
        return false;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text) {
        return false;
    }

    while (std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < 1.0 || v > 20.0) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}

bool rs7frets::ChordFrame(const float a0[3], const float a1[3], const float b[3], bool floorMoved, int top,
                          FrameFit* out) {
    float u[3], len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = a0[i] - a1[i];
        len2 += u[i] * u[i];
    }

    const float d = std::sqrt(len2);
    if (!(d > 1e-3f) || !(d < 1e3f)) {
        return false;
    }

    // Coordinate p along u (string 1 -> string 0 -> string 6), 0 at string 0: string i lies at p = -i d.
    float pb = 0.0f;

    for (int i = 0; i < 3; ++i) {
        pb += (b[i] - a0[i]) * u[i] / d;
    }

    const bool six = pb > 0.0f;                 // the origin edge lies beyond string 0 / 6's side
    const float m = six ? pb : -pb - 5.0f * d;  // stock margin of the frame beyond lanes 0 / 5
    if (!(m > 0.05f * d) || !(m < 4.0f * d) || !std::isfinite(m)) {
        return false;
    }

    const float h = 5.0f * d + 2.0f * m;                         // the model's extent
    const float po = pb + (six && floorMoved ? d : 0.0f);        // origin edge as placed now
    const float lo = -5.0f * d - m;                              // string 5 end, margin m
    const float hi = (top > 5 ? float(top - 5) * d : 0.0f) + m;  // string 6 / `top` (or string 0) end, margin m
    // The origin edge stays where the game puts it (on the floor); only the far edge moves, so the frame's height
    // grows. The origin edge moves out only when it would not reach the lane on its own side (the floor of a guitar
    // whose fret bars were not extended, or the 8th lane).
    float origin, farEdge;

    if (six) {
        origin = po + (hi > po ? hi - po : 0.0f);  // beyond string 6's side: p grows
        farEdge = lo;
    } else {
        origin = po - (po > lo ? po - lo : 0.0f);
        farEdge = hi;
    }

    out->scale = std::fabs(farEdge - origin) / h;
    out->shift = six ? po - origin : origin - po;  // along the extent, from the origin edge towards the lanes
    out->origin = po;
    out->margin = m;
    out->spacing = d;

    return std::isfinite(out->scale) && std::isfinite(out->shift) && out->scale > 0.0f;
}
