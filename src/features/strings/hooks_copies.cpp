// The copied GuitarStrings functions (strings.h "Loops"): building the copies at startup and the entry dispatch of
// their GS* sites (Rs7Gs_Target, called by the entry stubs in hooks_stubs.cpp).
#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/string7/string7.h"
#include "features/strings/fncopy_specs.generated.h"
#include "features/strings/hooks.h"
#include "model/groups.h"
#include "model/stringcolour.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

#define RS7_FNVARS(id)                                                                                                 \
    extern "C" __declspec(dllexport) uintptr_t g_rs7_fncopy_##id = 0;                                                  \
    extern "C" __declspec(dllexport) uintptr_t g_rs7_fntramp_##id = 0;
RS7_FNVARS(GSFR)
RS7_FNVARS(GSBD)
RS7_FNVARS(GSUL)
RS7_FNVARS(GSLD)
RS7_FNVARS(GSFO)
RS7_FNVARS(GSFI)
RS7_FNVARS(GSXF)
RS7_FNVARS(GSWD)
RS7_FNVARS(GSUP)
RS7_FNVARS(GSRS)
RS7_FNVARS(GSPK)
RS7_FNVARS(GSVB)
RS7_FNVARS(GSIC)
RS7_FNVARS(GSDM)
RS7_FNVARS(GSGL)
RS7_FNVARS(GSHC)
#undef RS7_FNVARS

using namespace rs7gs;

namespace {
struct FnVar {
    const char* id;
    uintptr_t* copy;
    uintptr_t* tramp;
};

#define RS7_FNV(id) {#id, &g_rs7_fncopy_##id, &g_rs7_fntramp_##id}
const FnVar kFnVars[] = {RS7_FNV(GSFR), RS7_FNV(GSBD), RS7_FNV(GSUL), RS7_FNV(GSLD), RS7_FNV(GSFO), RS7_FNV(GSFI),
                         RS7_FNV(GSXF), RS7_FNV(GSWD), RS7_FNV(GSUP), RS7_FNV(GSRS), RS7_FNV(GSPK), RS7_FNV(GSVB),
                         RS7_FNV(GSIC), RS7_FNV(GSDM), RS7_FNV(GSGL), RS7_FNV(GSHC)};
#undef RS7_FNV

struct Pair {
    uintptr_t* copy;
    uintptr_t* tramp;
};

#define RS7_P(id) {&g_rs7_fncopy_##id, &g_rs7_fntramp_##id}
// Indexed by Rs7GsKind.
const Pair kByKind[kGsKinds] = {RS7_P(GSFR), RS7_P(GSBD), RS7_P(GSUL), RS7_P(GSLD), RS7_P(GSFO), RS7_P(GSFI),
                                RS7_P(GSXF), RS7_P(GSWD), RS7_P(GSUP), RS7_P(GSRS), RS7_P(GSPK), RS7_P(GSVB),
                                RS7_P(GSIC), RS7_P(GSDM), RS7_P(GSGL), RS7_P(GSHC)};
#undef RS7_P

// [mode][string - 6][state][rgb]
float* Rgb(int mode, int s, int state) {
    return g_rs7_ns_rgb + ((mode * 2 + (s - 6)) * 3 + state) * 3;
}

void FillPalette() {
    if (g_rs7_native_colours) {
        g_rs7_ns_v[0] = g_rs7_ns_v[1] = g_rs7_s7_v;
        return;  // RGB lookups use the native matrix; no per-query extra palette.
    }

    const uintptr_t ref = g_rs7_colormgr;
    const uint8_t* mgr = ref ? *reinterpret_cast<uint8_t* const*>(ref) : nullptr;

    for (int mode = 0; mode < 2; ++mode) {
        // the stock dimmed value: state 2's value (max channel) of strings 0-5 (Rs7S7_DimColour)
        float v = rs7s7::kDefaultDim, sum = 0.0f;
        int n = 0;

        for (int s = 0; mgr && s < 6; ++s) {
            const float* c = *reinterpret_cast<const float* const*>(
                mgr + 0x350 + (mode * 28 * rs7colour::Width() + 2 * rs7colour::Width() + s) * 4);
            if (!c) {
                continue;
            }

            sum += c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            ++n;
        }

        if (n && sum > 0.0f) {
            v = sum / float(n);
        }
        const float* lit = g_rs7_fx_rgb + mode * 3;

        for (int s = 6; s < 8; ++s) {
            std::memcpy(Rgb(mode, s, 0), lit, 3 * sizeof(float));
            Rs7S7_DimColour(lit, v, Rgb(mode, s, 1));
            Rs7S7_GlowColour(lit, Rgb(mode, s, 2));
        }
    }

    g_rs7_ns_v[0] = g_rs7_ns_v[1] = g_rs7_s7_v;
}

// The GuitarStrings a kind's entry is about: its register, or found through the stack argument the kind names.
uint8_t* StringsOf(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    if (kind == kGsDim) {  // [esp+4] = session context: +0Ch -> +37Ch guitar -> +0Ch strings
        uint8_t* p = a1 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x0C) : nullptr;
        uint8_t* g = p ? Rd<uint8_t*>(p, 0x37C) : nullptr;

        return g ? Rd<uint8_t*>(g, 0x0C) : nullptr;
    }

    if (kind == kGsGlow) {  // [esp+4] = StringRenderer: +4 strings
        return a1 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a1), 0x04) : nullptr;
    }
    if (kind == kGsHeadCol) {  // (headstock, guitar)
        return a2 ? Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a2), 0x0C) : nullptr;
    }

    return reg;
}

// kGsFree releases all 8 pairs whatever the info.
int CountRelease(uint8_t* gs) {
    Rs7_S7Free(gs);  // N5's visibility entry (no 7th string object exists here)

    // The copy frees every non-null slot of the 8 pairs; the sized free follows (GSZ1) and clears
    // the marker, so count it here, where the marker is still set.
    for (int s = kStock; s < kCapacity; ++s) {
        if (Rd<uint8_t*>(gs, kMain + 4 * s)) {
            Rs7Alloc_Dec(kAkStringObjs);
        }
        if (Rd<uint8_t*>(gs, kGhost + 4 * s)) {
            Rs7Alloc_Dec(kAkStringObjs);
        }
    }

    Rs7Alloc_Dec(kAkGuitarStrings);

    return 1;
}

// Why the native copy must not run on this GuitarStrings (the stock code runs), nullptr = it may.
const char* Refusal(uint32_t kind, uint8_t* gs, uint8_t* info, uint32_t a2) {
    if (!Rs7Gs_InfoNative(info)) {
        return "its InstrumentInfo is not native";
    }
    if (!Rd<uint8_t>(gs, kTail + 3)) {
        return "its strings 6/7 could not be allocated";
    }

    uint8_t* owner = Rd<uint8_t*>(gs, 0);
    if (owner && Rd<uint8_t*>(owner, 0x24) != info) {
        return "owner +24h is not its InstrumentInfo";
    }

    if (kind == kGsLoad) {  // the load reads the info from (context +4) +24h
        uint8_t* ctx = reinterpret_cast<uint8_t*>(a2);
        uint8_t* g = ctx ? Rd<uint8_t*>(ctx, 0x04) : nullptr;
        if (!g || Rd<uint8_t*>(g, 0x24) != info) {
            return "load context's info differs";
        }
    } else if (kind == kGsInitCol) {
        if (reinterpret_cast<uint8_t*>(a2) != info) {
            return "InitColors info differs";
        }
    } else if (kind == kGsHeadCol) {
        if (Rd<uint8_t*>(reinterpret_cast<uint8_t*>(a2), 0x24) != info) {
            return "headstock guitar's info differs";
        }
    }

    return nullptr;
}
}  // namespace

extern "C" int __cdecl Rs7Gs_Use(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    if (!g_rs7_native_strings || kind >= kGsKinds) {
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

        uint8_t* info = Rd<uint8_t*>(gs, kInfo);

        if (const char* why = Refusal(kind, gs, info, a2)) {
            if (Upto(&g_strings.logReject[kind], 1)) {
                Rs7Log("gsnative: kind %u on strings %p runs stock: %s", kind, gs, why);
            }

            return 0;
        }

        if (kind == kGsLoad) {
            At<uint8_t>(gs, kTail + 1) += 1;
        } else if (kind == kGsInitCol) {
            FillPalette();
        }

        RefreshCounts(info, g_rs7_s7_on != 0);
        if (Upto(&g_strings.logUse[kind], 1)) {
            Rs7Log("gsnative: first native call kind %u (strings %p, loaded %u, active %u)", kind, gs,
                   Rd<uint8_t>(info, kInfoLoaded), Rd<uint8_t>(info, kInfoActive));
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

extern "C" uintptr_t __cdecl Rs7Gs_Target(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    const Pair& p = kByKind[kind];

    return (*p.copy && Rs7Gs_Use(kind, reg, a1, a2)) ? *p.copy : *p.tramp;
}

bool Rs7Gs_BuildCopies() {
    if (!Rs7Gs_PrepareCallIns()) {
        Rs7Log("gsnative: call-in entries not built - native strings off");
        return false;
    }

    for (int i = 0; i < kRs7FnSpecCount; ++i) {
        const Rs7FnSpec& sp = kRs7FnSpecs[i];
        const FnVar* v = nullptr;

        for (const FnVar& x : kFnVars) {
            if (std::strcmp(x.id, sp.id) == 0) {
                v = &x;
            }
        }

        char why[160] = {};
        uintptr_t c = 0, t = 0;

        if (!v || !Rs7Fn_Build(sp, sp.va, &c, &t, why, sizeof why)) {
            Rs7Log("gsnative: copy %s failed (%s) - native strings off", sp.id, v ? why : "no variables");

            for (const FnVar& x : kFnVars) {
                *x.copy = *x.tramp = 0;
            }

            return false;
        }

        *v->copy = c;
        *v->tramp = t;
    }

    FillPalette();
    Rs7Log("gsnative: %d function copies built (native N-slot GuitarStrings)", kRs7FnSpecCount);

    return true;
}

void Rs7Gs_Prepare() {
    if (g_rs7_native_strings && !Rs7Gs_BuildCopies()) {
        g_rs7_native_strings = 0;
    }
}
