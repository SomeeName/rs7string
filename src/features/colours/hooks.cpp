// The colour feature's game glue: the CM* ColourManager mid-hooks that are not about publication (CMUV, CMIX,
// CMFALL), RSMods' glow correction (GLW, Rsxr_GlowFixPtr) and startup.
#include "features/colours/state.h"
#include "model/groups.h"
#include "platform/freeze.h"
#include "platform/log.h"
#include "platform/safecopy.h"

extern "C" __declspec(dllexport) uintptr_t g_rsxr_glow_fix = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_colormgr = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_texlist_cs = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_texlist_head = 0;

namespace rsxrcolour {
State g_colour;
}  // namespace rsxrcolour

using rsxrcolour::g_colour;

// ---- startup

void RsxrColour_Start(RsxrColourConfig& cfg) {
    // Inspect the installed RSMods proxy without depending on its load order; keep saved colours intact.
    rsxrrsmods::Init();
    uint32_t effectiveRgb[2] = {cfg.string6Rgb, cfg.string6RgbCb};

    rsxrrsmods::Resolve(rsxrrsmods::Cached(), cfg.rsmodsExtendedRange, effectiveRgb);
    cfg.string6Rgb = effectiveRgb[0];
    cfg.string6RgbCb = effectiveRgb[1];
    rsxrcolour::SetPalette(effectiveRgb);
    const uint32_t rgb8[2] = {cfg.string7Rgb, cfg.string7RgbCb};

    rsxrcolour::SetPalette8(rgb8);  // the eighth string ([color] string7_color)
    RsxrLog("colour: RSMods Extended Range automatic=%d available=%d effective=%06X/%06X", cfg.rsmodsExtendedRange,
            rsxrrsmods::Cached().valid, unsigned(cfg.string6Rgb), unsigned(cfg.string6RgbCb));
    // RSMods' custom string colours leave a pure red string's glow outline near black (Rsxr_GlowFixPtr).
    g_rsxr_glow_fix = rsxrrsmods::CustomGlow() ? 1 : 0;
    RsxrLog("colour: RSMods custom glow correction=%u", unsigned(g_rsxr_glow_fix));
}

// The native layout widens the ColourManager at creation (CM* sites: 0xDD0 -> 0x1BD0 bytes, 6 -> 8 columns). The
// game builds its one manager once, ~12 s after launch (measured); one
// that already exists keeps the stock 6-column layout in a 0xDD0 block, which the 8-column readers would misread
// (rotated string colours, black 7th string) and a rebuild would overflow. Then the native matrix is refused.
void RsxrColour_CheckManager(uint32_t d3d9Tick) {
    if (!g_rsxr_native_colours) {
        return;
    }

    void* const mgr = rsxrcolour::ExistingManager(g_rsxr_colormgr);

    if (mgr) {
        RsxrLog("colour: the game built its ColourManager (%p) before the sites were written (%.1f s after "
                "d3d9) - native_colours OFF (stock six-column matrix)",
                mgr, (GetTickCount() - d3d9Tick) / 1000.0);
        g_rsxr_native_colours = 0;
    } else {
        RsxrLog("colour: ColourManager not built yet (%.1f s after d3d9) - native 8-column layout from its creation",
                (GetTickCount() - d3d9Tick) / 1000.0);
    }
}

bool RsxrColour_CanPatchFrozen(uintptr_t ctorSite, uintptr_t allocSite) {
    g_colour.patchRefusal = {};
    g_colour.patchReason = "missing-anchor";
    g_colour.patchRange = "none";
    void* manager = nullptr;

    if (!ctorSite || !allocSite) {
        g_colour.patchRefusal.address = ctorSite;
        g_colour.patchRefusal.value = allocSite;
        return false;
    }

    g_colour.patchRefusal.address = g_rsxr_colormgr;
    g_colour.patchReason = "unreadable-manager-slot";
    if (!g_rsxr_colormgr || !Rsxr_SafeCopy(&manager, reinterpret_cast<const void*>(g_rsxr_colormgr), sizeof manager)) {
        return false;
    }

    if (manager) {
        g_colour.patchReason = "manager-non-null";
        g_colour.patchRefusal.value = reinterpret_cast<uintptr_t>(manager);
        return false;
    }

    // Boundaries already identified by sites.json and gen_colournative.py: constructor 517F70..518550,
    // creation 518AC0..518AF0. Anchor them to resolved sites so relocation is preserved.
    const RsxrRange ctor{ctorSite - (0x5184F6 - 0x517F70), ctorSite + (0x518550 - 0x5184F6)};
    const RsxrRange alloc{allocSite - (0x518AC4 - 0x518AC0), allocSite + (0x518AF0 - 0x518AC4)};

    g_colour.patchRange = "constructor";

    if (Rsxr_FrozenThreadCallsRange(ctor, &g_colour.patchRefusal)) {
        g_colour.patchReason = Rsxr_FrozenReasonName(g_colour.patchRefusal.reason);
        return false;
    }

    g_colour.patchRange = "creation";

    if (Rsxr_FrozenThreadCallsRange(alloc, &g_colour.patchRefusal)) {
        g_colour.patchReason = Rsxr_FrozenReasonName(g_colour.patchRefusal.reason);
        return false;
    }

    g_colour.patchReason = "none";

    return true;
}

void RsxrColour_LogPatchRefusal() {
    const RsxrFrozenRefusal& r = g_colour.patchRefusal;

    RsxrLog("colour: frozen recheck refused reason=%s tid=%lu eip=%08X esp=%08X StackLimit=%08X StackBase=%08X "
            "address=%08X value=%08X range=%s[%08X,%08X) fs=%04lX teb=%08X error=%lu state=%08lX protect=%08lX",
            g_colour.patchReason, r.tid, unsigned(r.eip), unsigned(r.esp), unsigned(r.stackLimit),
            unsigned(r.stackBase), unsigned(r.address), unsigned(r.value), g_colour.patchRange, unsigned(r.range.lo),
            unsigned(r.range.hi), r.fs, unsigned(r.teb), r.error, r.state, r.protect);
}

// ---- ColourManager mid-hooks (CMINIT / CMBUILD: hooks_publish.cpp)

void __cdecl RsxrColour_CMUV(RsxrMid::Context& c) {
    // 518BC0: ST0 equals the bits in [EBP+14h], EAX=(mode*28+state)*8.
    // Replace first FST via integer store and skip it; preserve the x87 stack.
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0x14);

    for (uint32_t slot : {0u, 6u, 7u}) {
        rsxrcolour::PutBits(mgr, rsxrcolour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518BC7;
}

void __cdecl RsxrColour_CMIX(RsxrMid::Context& c) {
    // 518CA4: EDX=mode*28+14. Replace LEA *3 / ADD *2 with index*8.
    c.eax = c.edx * 8;
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(c.ebp + 0xC);
    auto* mgr = reinterpret_cast<uint8_t*>(c.ecx);

    for (uint32_t slot : {6u, 7u}) {
        rsxrcolour::PutBits(mgr, rsxrcolour::kUV + 4 * (c.eax + slot), bits);
    }

    c.jump = 0x518CA9;
}

void __cdecl RsxrColour_CMFALL(RsxrMid::Context& c) {
    // 75835A: bounded fallback before the guard; no six-slot stack OOB.
    if (c.edx < 6) {
        c.eax = *reinterpret_cast<const uint32_t*>(c.ebp + c.edx * 4 - 0x1C);
    } else if (c.edx == 6) {
        c.eax = g_colour.fallback7[RsxrColour_Mode()];
    } else {
        c.eax = c.edx == 7 ? rsxrcolour::Colour8(RsxrColour_Mode()) : 0;
    }
    c.esp -= 4;
    *reinterpret_cast<uint32_t*>(c.esp) = c.esi;  // replay PUSH ESI
    c.jump = 0x75835F;
}

// ---- RSMods' glow (GLW)

extern "C" const float* __cdecl Rsxr_GlowFixPtr(const float* glow, const uint8_t* mgr, uint32_t s) {
    if (!g_rsxr_glow_fix || !glow || !mgr || s >= 6) {
        return glow;
    }

    // The glow's own colour mode (UpdateGlow reads [mgr+348h], state 3); RSMods writes Enabled (state 1)
    // as the user's colour and derives the glow from it.
    const uint32_t mode = *reinterpret_cast<const uint32_t*>(mgr + 0x348);
    const float* enabled = rsxrcolour::GetRGB(mgr, mode, 1, s);
    if (!enabled) {
        return glow;
    }

    float* out = g_colour.glow[mode][s];

    return rsxrrsmods::CorrectGlow(true, enabled, glow, out) ? out : glow;
}

// GLW 0x54438B in Rnd_StringRenderer_UpdateGlow 0x5441A0 (stock function; the GSGL copy has
// call-in Rsxr_CallIn_GlowColour): EAX = the string's glow RGB pointer, ECX = colour manager, EDX = string. With
// RSMods' custom colours a pure red string's glow is near black (colours.h, RSMods): point EAX at a corrected copy.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_GLW(RsxrMid::Context& c) {
    c.eax = reinterpret_cast<uintptr_t>(
        Rsxr_GlowFixPtr(reinterpret_cast<const float*>(c.eax), reinterpret_cast<const uint8_t*>(c.ecx), c.edx));
}
