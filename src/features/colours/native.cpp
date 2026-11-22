// The native eight-column colour matrix: its layout in a ColourManager and the publication of the stock pointers
// plus the owned extra-string colours into it.
#include "features/colours/state.h"
#include "model/groups.h"
#include "model/stringcolour.h"
#include "platform/field.h"
#include <algorithm>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rsxr_native_colours = 0;
extern "C" __declspec(dllexport) float g_rsxr_v6[2] = {0.0f, 0.0f};
extern "C" __declspec(dllexport) float g_rsxr_fx_rgb[6] = {};

namespace {
using rsxrcolour::g_colour;
using rsxrcolour::kStates;
// The states whose entries are texture rows (UV), not colours (PublishExtraUV).
constexpr uint32_t kUvStates[] = {15, 17, 21};

using rsxrmem::UncheckedField;

float Max(const float* c) {
    return c ? std::max(c[0], std::max(c[1], c[2])) : 0.0f;
}

bool PerString(int state) {
    return state <= 4 || state == 7 || state == 8 || state == 11 || state == 12 || state == 13 || state == 15 ||
           state == 16 || state == 17 || state >= 25;
}

uint32_t NativeUV(uint32_t mode, uint32_t state, uint32_t slot) {
    return rsxrcolour::kUV + 4 * ((mode * kStates + state) * 8 + slot);
}

// CONFIRMED RSMods GetStringColor uses the stock state*6/mode*168 layout.
// Share the pointers, not RGB snapshots: custom colours and rainbow writes must
// immediately reach the game and must never target another state or mode.
void ShareStockColumns(uint8_t* mgr) {
    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t state = 0; state < kStates; ++state) {
            const uint32_t legacy = 4 * ((mode * kStates + state) * 6);

            std::memcpy(mgr + 0x350 + legacy, mgr + rsxrcolour::PtrOffset(mode, state), 6 * 4);
            std::memcpy(mgr + 0x890 + legacy, mgr + rsxrcolour::UVOffset(mode, state), 6 * 4);
        }
    }
}

// The stock strings' mean brightness in the dimmed state 2 (rsxrs7::kDefaultDim without any).
float DimOf(const uint8_t* mgr, uint32_t mode) {
    float dim = rsxrs7::kDefaultDim, total = 0.0f;
    int count = 0;

    for (int s = 0; s < 6; ++s) {
        const float* c = rsxrcolour::GetRGB(mgr, mode, 2, s);

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
// or string 7's ([color] string7_color, extra 1), dimmed, glowing or scaled like string 0 in that state.
void ExtraColour(const uint8_t* mgr, uint32_t mode, uint32_t state, int extra, float dim, float out[3]) {
    const float* stock = rsxrcolour::GetRGB(mgr, mode, state, 0);
    const float baseline = Max(rsxrcolour::GetRGB(mgr, mode, 1, 0));

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
        std::memcpy(lit, g_rsxr_fx_rgb + mode * 3, 12);
    } else {
        Rsxr_RgbToFloats(rsxrcolour::Colour8(mode), lit);
    }
    std::memcpy(out, lit, 12);

    if (state == 2) {
        RsxrS7_DimColour(lit, dim, out);
    } else if (state == 3) {
        RsxrS7_GlowColour(lit, out);
    } else if ((state == 0 || state == 4 || state == 7 || state == 8) && baseline > 0.0f) {
        const float scale = Max(stock) / baseline;

        for (int c = 0; c < 3; ++c) {
            out[c] = lit[c] * scale;
        }
    }
}

void PublishExtraRgb(uint8_t* mgr, uint32_t mode, float dim) {
    for (uint32_t state = 0; state < kStates; ++state) {
        for (int extra = 0; extra < 2; ++extra) {
            float* out = g_colour.rgb[mode][state][extra];

            ExtraColour(mgr, mode, state, extra, dim, out);
            // Capacity defaults also exist for shared/unknown states; their
            // existing indices and authored arities are not widened.
            UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 6 + extra)) = out;
        }
    }
}

void PublishExtraUV(uint8_t* mgr, uint32_t mode) {
    // Authored texture rows, never extrapolate UV or change denominators: string 6 uses the recoloured V6 band,
    // string 7 the V7 band painted with it (string7_color); without the bands both borrow a stock row.
    const bool bands = g_rsxr_v6[mode] != 0.0f;

    for (uint32_t state : kUvStates) {
        UncheckedField<float>(mgr, NativeUV(mode, state, 6)) =
            bands ? g_rsxr_v6[mode] : UncheckedField<float>(mgr, NativeUV(mode, state, 0));
        UncheckedField<float>(mgr, NativeUV(mode, state, 7)) =
            bands ? kRsxrV7[mode] : UncheckedField<float>(mgr, NativeUV(mode, state, 5));
    }

    // Wire UV denominator 6; scale-marker state14 is an index, not RGB.
    for (uint32_t state : {11u, 14u}) {
        rsxrcolour::PutBits(mgr, NativeUV(mode, state, 6), UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 0)));
        rsxrcolour::PutBits(mgr, NativeUV(mode, state, 7), UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 5)));
    }
}

// Slot 4 shows slot 6 (SetBassLowB): every state's pointer and the band states' UV. Called after ShareStockColumns, so
// the six-column tables RSMods reads keep the stock slot 4.
void SwapBassLowB(uint8_t* mgr) {
    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t state = 0; state < kStates; ++state) {
            g_colour.savedPtr4[mode][state] = UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 4));
            g_colour.savedUV4[mode][state] = UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 4));
            UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 4)) =
                UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 6));
        }

        for (uint32_t state : kUvStates) {
            rsxrcolour::PutBits(mgr, NativeUV(mode, state, 4), UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 6)));
        }

        for (uint32_t state = 0; state < kStates; ++state) {
            g_colour.swappedPtr4[mode][state] = UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 4));
            g_colour.swappedUV4[mode][state] = UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 4));
        }
    }

    g_colour.bassSwapped = true;
    g_colour.swappedMgr = mgr;
}

// Only entries that still hold what the swap wrote: CONFIRMED the game's table build (0x518D30, CMBUILD) rewrites
// slot 4 of every state it fills (0x518AF0 writes slots 0 to 5), so after it the saved values are stale.
void RestoreSlot4(uint8_t* mgr) {
    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t state = 0; state < kStates; ++state) {
            float*& ptr = UncheckedField<float*>(mgr, rsxrcolour::PtrOffset(mode, state, 4));

            if (ptr == g_colour.swappedPtr4[mode][state]) {
                ptr = g_colour.savedPtr4[mode][state];
            }

            if (UncheckedField<uint32_t>(mgr, NativeUV(mode, state, 4)) == g_colour.swappedUV4[mode][state]) {
                rsxrcolour::PutBits(mgr, NativeUV(mode, state, 4), g_colour.savedUV4[mode][state]);
            }
        }
    }

    g_colour.bassSwapped = false;
}
}  // namespace

void rsxrcolour::SetBassLowB(bool on) {
    if (InterlockedExchange(&g_colour.bassLowB, on ? 1 : 0) != (on ? 1 : 0)) {
        RequestPublish();
    }
}

const float* rsxrcolour::GetRGB(const uint8_t* mgr, uint32_t mode, uint32_t state, uint32_t slot) {
    if (!mgr || mode >= 2 || state >= kStates || slot >= Width()) {
        return nullptr;
    }

    return *reinterpret_cast<const float* const*>(mgr + PtrOffset(mode, state, slot));
}

void* rsxrcolour::ExistingManager(uintptr_t ref) {
    return ref ? *reinterpret_cast<void* const volatile*>(ref) : nullptr;
}

void rsxrcolour::SetPalette(const uint32_t rgb[2]) {
    AcquireSRWLockExclusive(&g_colour.paletteLock);
    std::memcpy(g_colour.pendingPalette, rgb, sizeof g_colour.pendingPalette);
    g_colour.palettePending = true;
    ReleaseSRWLockExclusive(&g_colour.paletteLock);
    RequestPublish();
    ApplyPendingPalette();
}

void rsxrcolour::ApplyPendingPalette() {
    AcquireSRWLockExclusive(&g_colour.paletteLock);

    if (!g_colour.palettePending || (g_colour.cmTid && g_colour.cmTid != GetCurrentThreadId())) {
        ReleaseSRWLockExclusive(&g_colour.paletteLock);
        return;
    }

    for (int mode = 0; mode < 2; ++mode) {
        Rsxr_RgbToFloats(g_colour.pendingPalette[mode], g_rsxr_fx_rgb + mode * 3);
        g_colour.fallback7[mode] = g_colour.pendingPalette[mode] & 0xFFFFFF;
    }

    g_colour.palettePending = false;
    ReleaseSRWLockExclusive(&g_colour.paletteLock);
}

void rsxrcolour::SetPalette8(const uint32_t rgb[2]) {
    for (int mode = 0; mode < 2; ++mode) {
        g_colour.rgb8[mode] = rgb[mode] & 0xFFFFFF;
    }
}

uint32_t rsxrcolour::Colour8(uint32_t mode) {
    return g_colour.rgb8[mode & 1];
}

void rsxrcolour::Publish(uint8_t* mgr) {
    if (!g_rsxr_native_colours || !mgr) {
        return;
    }

    ApplyPendingPalette();

    if (g_colour.bassSwapped && mgr == g_colour.swappedMgr) {
        RestoreSlot4(mgr);
    }
    g_colour.bassSwapped = false;  // another manager: its slot 4 is stock, the saved pointers belong to the old one
    ShareStockColumns(mgr);

    for (uint32_t mode = 0; mode < 2; ++mode) {
        PublishExtraRgb(mgr, mode, DimOf(mgr, mode));
        PublishExtraUV(mgr, mode);
    }

    if (g_colour.bassLowB) {
        SwapBassLowB(mgr);
    }
}
