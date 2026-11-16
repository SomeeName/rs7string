// The fret bars' logic (frets.h): the bar extension, the highway-floor shift, the [neck] fret_halflen parser and the
// chord-frame fit. Values in, values out; the game glue is hooks.cpp.
#include "features/frets/frets.h"
#include <cmath>

bool rs7frets::Compute(const float a0[3], const float a1[3], const float mid[3], float halfLen, int lanes, Extend* out,
                       int far, int farLanes) {
    float d[3], len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        d[i] = a0[i] - a1[i];
        len2 += d[i] * d[i];
    }

    const float len = std::sqrt(len2);
    if (!(len > 1e-3f) || !(len < 1e3f) || !(halfLen > 0.0f) || lanes < 1 || lanes > 2 || far < 1 || far > 5 ||
        farLanes < 0 || farLanes > 1) {
        return false;
    }

    // Along u (unit, string 1 -> string 0 -> string 6), relative to string 0: the stock bar spans c -+ L.
    float c = 0.0f;

    for (int i = 0; i < 3; ++i) {
        c += (mid[i] - a0[i]) * d[i] / len;
    }

    const float m0 = c + halfLen;                               // margin beyond string 0
    const float m5 = (c - halfLen) * -1.0f - float(far) * len;  // margin beyond the far string (at -far len)
    const float margin = m0 > m5 ? m0 : m5;                     // the wider stock margin, now beyond the outer string
    // The far end stays, unless farLanes strings lie beyond the far string: then it moves that many spacings out.
    const float top = float(lanes) * len + margin, bottom = c - halfLen - float(farLanes) * len;
    const float along = 0.5f * (top + bottom) - c;  // centre shift along u

    out->scale = 0.5f * (top - bottom) / halfLen;

    for (int i = 0; i < 3; ++i) {
        out->shift[i] = along * d[i] / len;
    }

    out->spacing = len;

    return out->scale > 0.0f && std::isfinite(out->scale);
}

void rs7frets::ScaleBarMid(const float a0[3], const float a1[3], float k, float mid[3], float moved[3]) {
    float u[3], len2 = 0.0f, along = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = a0[i] - a1[i];
        len2 += u[i] * u[i];
    }

    const float len = std::sqrt(len2);

    for (int i = 0; i < 3; ++i) {
        u[i] = len > 1e-6f ? u[i] / len : 0.0f;
        along += mid[i] * u[i];
    }

    for (int i = 0; i < 3; ++i) {
        moved[i] = (k - 1.0f) * along * u[i];
        mid[i] += moved[i];
    }
}

bool rs7frets::FloorShift(const float w0[3], const float w1[3], const float b[3], int lanes, float out[3]) {
    float u[3], len2 = 0.0f, side = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = w0[i] - w1[i];
        len2 += u[i] * u[i];
        side += (b[i] - w0[i]) * u[i];
    }

    if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f) || lanes < 1 || lanes > 2) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        out[i] = float(lanes) * u[i];
    }

    return true;
}

bool rs7frets::GlowEnd(const float w0[3], const float w1[3], int lanes, float p[3]) {
    float u[3], side = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = w0[i] - w1[i];
        side += (p[i] - w0[i]) * u[i];
    }

    if (lanes < 1 || lanes > 2 || !(side > 0.0f) || !std::isfinite(side)) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        p[i] += float(lanes) * u[i];
    }

    return true;
}

bool rs7frets::GlowEndAt(const float w0[3], const float w1[3], float beyond, float p[3]) {
    float u[3], len2 = 0.0f, side = 0.0f;

    for (int i = 0; i < 3; ++i) {
        u[i] = w0[i] - w1[i];
        len2 += u[i] * u[i];
        side += (p[i] - w0[i]) * u[i];
    }

    if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f) || !std::isfinite(side) || !(beyond > 0.0f) ||
        !(beyond < 8.0f)) {
        return false;
    }

    const float delta = beyond - side / len2;  // in string spacings, along u
    if (std::fabs(delta) < 1e-3f) {
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        p[i] += delta * u[i];
    }

    return true;
}

int rs7frets::FloorMove(const float a0[3], const float a1[3], const float b[3], int nearLanes, int far, int farLanes,
                        float out[3]) {
    float len2 = 0.0f;

    for (int i = 0; i < 3; ++i) {
        const float d = a0[i] - a1[i];

        len2 += d * d;
    }

    if (!(len2 > 1e-6f) || !(len2 < 1e6f)) {
        return -1;
    }

    if (FloorShift(a0, a1, b, nearLanes, out)) {
        return nearLanes;
    }
    if (farLanes < 1 || far < 1 || far > 5) {
        return 0;
    }

    float f0[3], f1[3];  // the stock far string and its neighbour (string i at a0 + i (a1 - a0))

    for (int i = 0; i < 3; ++i) {
        f0[i] = a0[i] + float(far) * (a1[i] - a0[i]);
        f1[i] = a0[i] + float(far - 1) * (a1[i] - a0[i]);
    }

    return FloorShift(f0, f1, b, farLanes, out) ? farLanes : 0;
}

bool rs7frets::ChordFrame(const float a0[3], const float a1[3], const float b[3], int floorLanes, int top,
                          FrameFit* out, int far, int farLanes) {
    if (far < 1 || far > 5 || farLanes < 0 || farLanes > 1) {
        return false;
    }

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

    const float fd = float(far) * d;      // the stock far string (5, bass 3) at p = -fd
    const bool six = pb > 0.0f;           // the origin edge lies beyond string 0 / 6's side
    const float m = six ? pb : -pb - fd;  // stock margin of the frame beyond lanes 0 / far
    if (!(m > 0.05f * d) || !(m < 4.0f * d) || !std::isfinite(m)) {
        return false;
    }

    const float h = fd + 2.0f * m;                                       // the model's extent
    const float po = pb + (six ? 1.0f : -1.0f) * float(floorLanes) * d;  // origin edge as placed now (FloorMove)
    const float lo = -fd - float(farLanes) * d - m;                      // the far string's end (+ farLanes), margin m
    const float hi = (top > 5 ? float(top - 5) * d : 0.0f) + m;          // string 6 / `top` (or string 0) end, margin m
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

float rs7frets::GlowStretch(int lanes, float spacing, float margin) {
    const float m = std::isfinite(margin) && margin > 0.0f ? 2.0f * margin : 0.0f;
    const float k = (float(lanes) * spacing + m) / (kStockStringsWidth + m);

    return std::isfinite(k) && k > 1.0f ? std::fmin(k, 3.0f) : 1.0f;
}
