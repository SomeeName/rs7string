// The copied GuitarStrings functions (strings.h "Loops"): building the copies at startup and the entry dispatch of
// their GS* sites (RsxrGs_Target, called by the entry stubs in hooks_stubs.cpp).
#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/string7/string7.h"
#include "features/strings/fncopy_specs.generated.h"
#include "features/strings/hooks.h"
#include "model/groups.h"
#include "model/instrument.h"
#include "model/stringcolour.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstring>

#define RSXR_FNVARS(id)                                                                                                \
    extern "C" __declspec(dllexport) uintptr_t g_rsxr_fncopy_##id = 0;                                                 \
    extern "C" __declspec(dllexport) uintptr_t g_rsxr_fntramp_##id = 0;
RSXR_FNVARS(GSFR)
RSXR_FNVARS(GSBD)
RSXR_FNVARS(GSUL)
RSXR_FNVARS(GSLD)
RSXR_FNVARS(GSFO)
RSXR_FNVARS(GSFI)
RSXR_FNVARS(GSXF)
RSXR_FNVARS(GSWD)
RSXR_FNVARS(GSUP)
RSXR_FNVARS(GSRS)
RSXR_FNVARS(GSPK)
RSXR_FNVARS(GSVB)
RSXR_FNVARS(GSIC)
RSXR_FNVARS(GSDM)
RSXR_FNVARS(GSGL)
RSXR_FNVARS(GSHC)
#undef RSXR_FNVARS

using namespace rsxrgs;

namespace {
struct FnVar {
    const char* id;
    uintptr_t* copy;
    uintptr_t* tramp;
};

#define RSXR_FNV(id) {#id, &g_rsxr_fncopy_##id, &g_rsxr_fntramp_##id}
// Indexed by RsxrGsKind.
const FnVar kFnVars[] = {RSXR_FNV(GSFR), RSXR_FNV(GSBD), RSXR_FNV(GSUL), RSXR_FNV(GSLD), RSXR_FNV(GSFO), RSXR_FNV(GSFI),
                         RSXR_FNV(GSXF), RSXR_FNV(GSWD), RSXR_FNV(GSUP), RSXR_FNV(GSRS), RSXR_FNV(GSPK), RSXR_FNV(GSVB),
                         RSXR_FNV(GSIC), RSXR_FNV(GSDM), RSXR_FNV(GSGL), RSXR_FNV(GSHC)};
#undef RSXR_FNV
static_assert(sizeof kFnVars / sizeof kFnVars[0] == kGsKinds, "kFnVars: one entry per RsxrGsKind, in its order");

float* Rgb(int mode, int s, int state) {
    return RsxrGs_NsRgb(uint32_t(mode), uint32_t(s), uint32_t(state));
}

void FillPalette() {
    if (g_rsxr_native_colours) {
        g_rsxr_ns_v[0] = g_rsxr_ns_v[1] = rsxrs7::kDefaultAtlasRow;
        return;  // RGB lookups use the native matrix; no per-query extra palette.
    }

    const uintptr_t ref = g_rsxr_colormgr;
    const uint8_t* mgr = ref ? *reinterpret_cast<uint8_t* const*>(ref) : nullptr;

    for (int mode = 0; mode < 2; ++mode) {
        // the stock dimmed value: state 2's value (max channel) of strings 0-5 (RsxrS7_DimColour)
        float v = rsxrs7::kDefaultDim, sum = 0.0f;
        int n = 0;

        for (int s = 0; mgr && s < 6; ++s) {
            const float* c = *reinterpret_cast<const float* const*>(
                mgr + 0x350 + (mode * 28 * rsxrcolour::Width() + 2 * rsxrcolour::Width() + s) * 4);
            if (!c) {
                continue;
            }

            sum += c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            ++n;
        }

        if (n && sum > 0.0f) {
            v = sum / float(n);
        }
        const float* lit = g_rsxr_fx_rgb + mode * 3;

        for (int s = 6; s < 8; ++s) {
            std::memcpy(Rgb(mode, s, 0), lit, 3 * sizeof(float));
            RsxrS7_DimColour(lit, v, Rgb(mode, s, 1));
            RsxrS7_GlowColour(lit, Rgb(mode, s, 2));
        }
    }

    g_rsxr_ns_v[0] = g_rsxr_ns_v[1] = rsxrs7::kDefaultAtlasRow;
}

// The GuitarStrings a kind's entry is about: its register, or found through the stack argument the kind names.
uint8_t* StringsOf(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    if (kind == kGsDim) {  // [esp+4] = session context: +0Ch -> +37Ch guitar -> +0Ch strings
        uint8_t* p = a1 ? UncheckedRead<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x0C) : nullptr;
        uint8_t* g = p ? UncheckedRead<uint8_t*>(p, rsxr::kSessionGuitar) : nullptr;

        return g ? UncheckedRead<uint8_t*>(g, rsxr::kGuitarStrings) : nullptr;
    }

    if (kind == kGsGlow) {  // [esp+4] = StringRenderer: +4 strings
        return a1 ? UncheckedRead<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x04) : nullptr;
    }
    if (kind == kGsHeadCol) {  // (headstock, guitar)
        return a2 ? UncheckedRead<uint8_t*>(reinterpret_cast<uint8_t*>(a2), rsxr::kGuitarStrings) : nullptr;
    }

    return reg;
}

// kGsFree releases all 8 pairs whatever the info.
int CountRelease(uint8_t* gs) {
    Rsxr_S7Free(gs);  // N5's visibility entry (no 7th string object exists here)

    // The copy frees every non-null slot of the 8 pairs; the sized free follows (GSZ1) and clears
    // the marker, so count it here, where the marker is still set.
    for (int s = kStock; s < kCapacity; ++s) {
        if (UncheckedRead<uint8_t*>(gs, kMain + 4 * s)) {
            RsxrAlloc_Dec(kAkStringObjs);
        }
        if (UncheckedRead<uint8_t*>(gs, kGhost + 4 * s)) {
            RsxrAlloc_Dec(kAkStringObjs);
        }
    }

    RsxrAlloc_Dec(kAkGuitarStrings);

    return 1;
}

// Why the native copy must not run on this GuitarStrings (the stock code runs), nullptr = it may.
const char* Refusal(uint32_t kind, uint8_t* gs, uint8_t* info, uint32_t a2) {
    if (!RsxrGs_InfoNative(info)) {
        return "its InstrumentInfo is not native";
    }
    if (!UncheckedRead<uint8_t>(gs, kTail + 3)) {
        return "its strings 6/7 could not be allocated";
    }

    uint8_t* owner = UncheckedRead<uint8_t*>(gs, 0);
    if (owner && UncheckedRead<uint8_t*>(owner, rsxr::kGuitarInfo) != info) {
        return "owner +24h is not its InstrumentInfo";
    }

    if (kind == kGsLoad) {  // the load reads the info from (context +4) +24h
        uint8_t* ctx = reinterpret_cast<uint8_t*>(a2);
        uint8_t* g = ctx ? UncheckedRead<uint8_t*>(ctx, 0x04) : nullptr;
        if (!g || UncheckedRead<uint8_t*>(g, rsxr::kGuitarInfo) != info) {
            return "load context's info differs";
        }
    } else if (kind == kGsInitCol) {
        if (reinterpret_cast<uint8_t*>(a2) != info) {
            return "InitColors info differs";
        }
    } else if (kind == kGsHeadCol) {
        if (UncheckedRead<uint8_t*>(reinterpret_cast<uint8_t*>(a2), rsxr::kGuitarInfo) != info) {
            return "headstock guitar's info differs";
        }
    }

    return nullptr;
}
}  // namespace

extern "C" int __cdecl RsxrGs_Use(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    if (!g_rsxr_native_strings || kind >= kGsKinds) {
        return 0;
    }

    __try {
        uint8_t* gs = StringsOf(kind, reg, a1, a2);
        if (!IsNative(gs)) {
            return 0;
        }
        if (kind == kGsFree) {
            return CountRelease(gs);
        }

        uint8_t* info = UncheckedRead<uint8_t*>(gs, kInfo);

        if (const char* why = Refusal(kind, gs, info, a2)) {
            if (RsxrLog_Upto(&g_strings.logReject[kind], 1)) {
                RsxrLog("gsnative: kind %u on strings %p runs stock: %s", kind, gs, why);
            }

            return 0;
        }

        if (kind == kGsLoad) {
            UncheckedField<uint8_t>(gs, kTail + 1) += 1;
        } else if (kind == kGsInitCol) {
            FillPalette();
        }

        RefreshCounts(info, g_rsxr_s7_on != 0);
        if (RsxrLog_Upto(&g_strings.logUse[kind], 1)) {
            RsxrLog("gsnative: first native call kind %u (strings %p, loaded %u, active %u)", kind, gs,
                    UncheckedRead<uint8_t>(info, kInfoLoaded), UncheckedRead<uint8_t>(info, kInfoActive));
        }
        return 1;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

extern "C" uintptr_t __cdecl RsxrGs_Target(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    const FnVar& p = kFnVars[kind];

    return (*p.copy && RsxrGs_Use(kind, reg, a1, a2)) ? *p.copy : *p.tramp;
}

bool RsxrGs_BuildCopies() {
    if (!RsxrGs_PrepareCallIns()) {
        RsxrLog("gsnative: call-in entries not built - native strings off");
        return false;
    }

    for (int i = 0; i < kRsxrFnSpecCount; ++i) {
        const RsxrFnSpec& sp = kRsxrFnSpecs[i];
        const FnVar* v = nullptr;

        for (const FnVar& x : kFnVars) {
            if (std::strcmp(x.id, sp.id) == 0) {
                v = &x;
            }
        }

        char why[160] = {};
        uintptr_t c = 0, t = 0;

        if (!v || !RsxrFn_Build(sp, sp.va, &c, &t, why, sizeof why)) {
            RsxrLog("gsnative: copy %s failed (%s) - native strings off", sp.id, v ? why : "no variables");

            for (const FnVar& x : kFnVars) {
                *x.copy = *x.tramp = 0;
            }

            return false;
        }

        *v->copy = c;
        *v->tramp = t;
    }

    FillPalette();
    RsxrLog("gsnative: %d function copies built (native N-slot GuitarStrings)", kRsxrFnSpecCount);

    return true;
}

void RsxrGs_Prepare() {
    if (g_rsxr_native_strings && !RsxrGs_BuildCopies()) {
        g_rsxr_native_strings = 0;
    }
}
