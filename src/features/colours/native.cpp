// The native eight-column colour matrix: its layout in a ColourManager and the publication of the stock pointers
// plus the owned extra-string colours into it.
#include "features/colours/state.h"
#include "model/groups.h"
#include "model/stringcolour.h"
#include <algorithm>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_colours = 0;
extern "C" __declspec(dllexport) float g_rs7_v6[2] = {0.0f, 0.0f};
extern "C" __declspec(dllexport) float g_rs7_fx_rgb[6] = {};

namespace {
using rs7colour::g_colour;

template <class T>
T& At(uint8_t* p, uint32_t o) {
    return *reinterpret_cast<T*>(p + o);
}

float Max(const float* c) {
    return c ? std::max(c[0], std::max(c[1], c[2])) : 0.0f;
}

bool PerString(int state) {
    return state <= 4 || state == 7 || state == 8 || state == 11 || state == 12 || state == 13 || state == 15 ||
           state == 16 || state == 17 || state >= 25;
}

uint32_t NativeUV(uint32_t mode, uint32_t state, uint32_t slot) {
    return rs7colour::kUV + 4 * ((mode * 28 + state) * 8 + slot);
}

// CONFIRMED RSMods GetStringColor uses the stock state*6/mode*168 layout.
// Share the pointers, not RGB snapshots: custom colours and rainbow writes must
// immediately reach the game and must never target another state or mode.
void ShareStockColumns(uint8_t* mgr) {
    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t state = 0; state < 28; ++state) {
            const uint32_t legacy = 4 * ((mode * 28 + state) * 6);

            std::memcpy(mgr + 0x350 + legacy, mgr + rs7colour::PtrOffset(mode, state), 6 * 4);
            std::memcpy(mgr + 0x890 + legacy, mgr + rs7colour::UVOffset(mode, state), 6 * 4);
        }
    }
}

// The stock strings' mean brightness in the dimmed state 2 (rs7s7::kDefaultDim without any).
float DimOf(const uint8_t* mgr, uint32_t mode) {
    float dim = rs7s7::kDefaultDim, total = 0.0f;
    int count = 0;

    for (int s = 0; s < 6; ++s) {
        const float* c = rs7colour::GetRGB(mgr, mode, 2, s);

        if (c) {
            total += Max(c);
            ++count;
        }
    }

    if (count && total > 0.0f) {
        dim = total / float(count);
    }

    return dim;
}

// The colour of extra string 6 + extra in `state`: string 0's for a shared state, else string 6's palette (extra 0)
// or the placeholder (extra 1), dimmed, glowing or scaled like string 0 in that state.
void ExtraColour(const uint8_t* mgr, uint32_t mode, uint32_t state, int extra, float dim, float out[3]) {
    const float* stock = rs7colour::GetRGB(mgr, mode, state, 0);
    const float baseline = Max(rs7colour::GetRGB(mgr, mode, 1, 0));

    if (stock) {
        std::memcpy(out, stock, 12);
    } else {
        std::memset(out, 0, 12);
    }

    if (!PerString(state)) {
        return;
    }

    float lit[3];

    if (extra == 0) {
        std::memcpy(lit, g_rs7_fx_rgb + mode * 3, 12);
    } else {
        Rs7_RgbToFloats(rs7colour::kPlaceholder[mode], lit);
    }
    std::memcpy(out, lit, 12);

    if (state == 2) {
        Rs7S7_DimColour(lit, dim, out);
    } else if (state == 3) {
        Rs7S7_GlowColour(lit, out);
    } else if ((state == 0 || state == 4 || state == 7 || state == 8) && baseline > 0.0f) {
        const float scale = Max(stock) / baseline;

        for (int c = 0; c < 3; ++c) {
            out[c] = lit[c] * scale;
        }
    }
}

void PublishExtraRgb(uint8_t* mgr, uint32_t mode, float dim) {
    for (uint32_t state = 0; state < 28; ++state) {
        for (int extra = 0; extra < 2; ++extra) {
            float* out = g_colour.rgb[mode][state][extra];

            ExtraColour(mgr, mode, state, extra, dim, out);
            // Capacity defaults also exist for shared/unknown states; their
            // existing indices and authored arities are not widened.
            At<float*>(mgr, rs7colour::PtrOffset(mode, state, 6 + extra)) = out;
        }
    }
}

void PublishExtraUV(uint8_t* mgr, uint32_t mode) {
    // Authored texture rows, never extrapolate UV or change denominators:
    // string 7 uses the recoloured ramp row; string 8 deliberately borrows
    // stock string 5's row until its own atlas band is authored.
    for (uint32_t state : {15u, 17u, 21u}) {
        const float v = g_rs7_v6[mode] != 0.0f ? g_rs7_v6[mode] : At<float>(mgr, NativeUV(mode, state, 0));

        At<float>(mgr, NativeUV(mode, state, 6)) = v;
        At<float>(mgr, NativeUV(mode, state, 7)) = At<float>(mgr, NativeUV(mode, state, 5));
    }

    // Wire UV denominator 6; scale-marker state14 is an index, not RGB.
    for (uint32_t state : {11u, 14u}) {
        rs7colour::PutBits(mgr, NativeUV(mode, state, 6), At<uint32_t>(mgr, NativeUV(mode, state, 0)));
        rs7colour::PutBits(mgr, NativeUV(mode, state, 7), At<uint32_t>(mgr, NativeUV(mode, state, 5)));
    }
}
}  // namespace

const float* rs7colour::GetRGB(const uint8_t* mgr, uint32_t mode, uint32_t state, uint32_t slot) {
    if (!mgr || mode >= 2 || state >= 28 || slot >= Width()) {
        return nullptr;
    }

    return *reinterpret_cast<const float* const*>(mgr + PtrOffset(mode, state, slot));
}

void* rs7colour::ExistingManager(uintptr_t ref) {
    return ref ? *reinterpret_cast<void* const volatile*>(ref) : nullptr;
}

void rs7colour::SetPalette(const uint32_t rgb[2]) {
    for (int mode = 0; mode < 2; ++mode) {
        Rs7_RgbToFloats(rgb[mode], g_rs7_fx_rgb + mode * 3);
    }
}

void rs7colour::Publish(uint8_t* mgr) {
    if (!g_rs7_native_colours || !mgr) {
        return;
    }

    ShareStockColumns(mgr);
    g_colour.fallback7 = (uint32_t(g_rs7_fx_rgb[0] * 255.0f) << 16) | (uint32_t(g_rs7_fx_rgb[1] * 255.0f) << 8) |
                         uint32_t(g_rs7_fx_rgb[2] * 255.0f);

    for (uint32_t mode = 0; mode < 2; ++mode) {
        PublishExtraRgb(mgr, mode, DimOf(mgr, mode));
        PublishExtraUV(mgr, mode);
    }
}
