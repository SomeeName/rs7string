// The colour feature's game glue: the CM* ColourManager mid-hooks that are not about publication (CMUV, CMIX,
// CMFALL), RSMods' glow correction (GLW, Rs7_GlowFixPtr) and startup.
#include "features/colours/state.h"
#include "model/groups.h"
#include "platform/log.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_glow_fix = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_colormgr = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_cs = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_head = 0;

namespace rs7colour {
State g_colour;
}  // namespace rs7colour

using rs7colour::g_colour;

// ---- startup

void Rs7Colour_Start(Rs7ColourConfig& cfg) {
    // Inspect the installed RSMods proxy without depending on its load order; keep saved colours intact.
    rs7rsmods::Init();
    uint32_t effectiveRgb[2] = {cfg.string6Rgb, cfg.string6RgbCb};

    rs7rsmods::Resolve(rs7rsmods::Cached(), cfg.rsmodsExtendedRange, effectiveRgb);
    cfg.string6Rgb = effectiveRgb[0];
    cfg.string6RgbCb = effectiveRgb[1];
    rs7colour::SetPalette(effectiveRgb);
    Rs7Log("colour: RSMods Extended Range automatic=%d available=%d effective=%06X/%06X", cfg.rsmodsExtendedRange,
           rs7rsmods::Cached().valid, unsigned(cfg.string6Rgb), unsigned(cfg.string6RgbCb));
    // RSMods' custom string colours leave a pure red string's glow outline near black (Rs7_GlowFixPtr).
    g_rs7_glow_fix = rs7rsmods::CustomGlow() ? 1 : 0;
    Rs7Log("colour: RSMods custom glow correction=%u", unsigned(g_rs7_glow_fix));
}

// The native layout widens the ColourManager at creation (CM* sites: 0xDD0 -> 0x1BD0 bytes, 6 -> 8 columns). The
// game builds its one manager once, ~12 s after launch (fix/colours2, measured with tests/colours2/cmtime.py); one
// that already exists keeps the stock 6-column layout in a 0xDD0 block, which the 8-column readers would misread
// (rotated string colours, black 7th string) and a rebuild would overflow. Then the native matrix is refused.
void Rs7Colour_CheckManager(uint32_t d3d9Tick) {
    if (!g_rs7_native_colours) {
        return;
    }

    void* const mgr = rs7colour::ExistingManager(g_rs7_colormgr);

    if (mgr) {
        Rs7Log("colour: the game built its ColourManager (%p) before the sites were written (%.1f s after "
               "d3d9) - native_colours OFF (stock six-column matrix)",
               mgr, (GetTickCount() - d3d9Tick) / 1000.0);
        g_rs7_native_colours = 0;
    } else {
        Rs7Log("colour: ColourManager not built yet (%.1f s after d3d9) - native 8-column layout from its creation",
               (GetTickCount() - d3d9Tick) / 1000.0);
    }
}

// ---- ColourManager mid-hooks (CMINIT / CMBUILD: hooks_publish.cpp)

void __cdecl Rs7Colour_CMUV(Rs7Mid::Context& c) {
    // 518BC0: ST0 equals the bits in [EBP+14h], EAX=(mode*28+state)*8.
    // Replace first FST via integer store and skip it; preserve the x87 stack.
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0x14);

    for (uint32_t slot : {0u, 6u, 7u}) {
        rs7colour::PutBits(mgr, rs7colour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518BC7;
}

void __cdecl Rs7Colour_CMIX(Rs7Mid::Context& c) {
    // 518CA4: EDX=mode*28+14. Replace LEA *3 / ADD *2 with index*8.
    c.eax = c.edx * 8;
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0xC);
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);

    for (uint32_t slot : {6u, 7u}) {
        rs7colour::PutBits(mgr, rs7colour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518CA9;
}

void __cdecl Rs7Colour_CMFALL(Rs7Mid::Context& c) {
    // 75835A: bounded fallback before the guard; no six-slot stack OOB.
    if (c.edx < 6) {
        c.eax = *reinterpret_cast<const uint32_t*>(c.ebp + c.edx * 4 - 0x1C);
    } else if (c.edx == 6) {
        c.eax = g_colour.fallback7;
    } else {
        c.eax = c.edx == 7 ? rs7colour::kPlaceholder[0] : 0;
    }
    c.esp -= 4;
    *reinterpret_cast<uint32_t*>(c.esp) = c.esi;  // replay PUSH ESI
    c.jump = 0x75835F;
}

// ---- RSMods' glow (GLW)

extern "C" const float* __cdecl Rs7_GlowFixPtr(const float* glow, const uint8_t* mgr, uint32_t s) {
    if (!g_rs7_glow_fix || !glow || !mgr || s >= 6) {
        return glow;
    }

    // The glow's own colour mode (UpdateGlow reads [mgr+348h], state 3); RSMods writes Enabled (state 1)
    // as the user's colour and derives the glow from it.
    const uint32_t mode = *reinterpret_cast<const uint32_t*>(mgr + 0x348);
    const float* enabled = rs7colour::GetRGB(mgr, mode, 1, s);
    if (!enabled) {
        return glow;
    }

    float* out = g_colour.glow[mode][s];

    return rs7rsmods::CorrectGlow(true, enabled, glow, out) ? out : glow;
}

// GLW 0x54438B in Rnd_StringRenderer_UpdateGlow 0x5441A0 (stock function; the GSGL copy has
// call-in Rs7_CallIn_GlowColour): EAX = the string's glow RGB pointer, ECX = colour manager, EDX = string. With RSMods'
// custom colours a pure red string's glow is near black (colours.h, RSMods): point EAX at a corrected copy.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GLW(Rs7Mid::Context& c) {
    c.eax = reinterpret_cast<uintptr_t>(
        Rs7_GlowFixPtr(reinterpret_cast<const float*>(c.eax), reinterpret_cast<const uint8_t*>(c.ecx), c.edx));
}
