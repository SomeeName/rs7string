#pragma once
#include "features/strings/strings.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <cstdint>
#include <intrin.h>

// The native GuitarStrings' game glue inside the feature: the copies' variables, the link variables of the sites,
// the stubs and mid-hook callbacks registration.cpp lists, the feature's state and its calls into the game. Not
// for code outside src/features/strings/.

// The copy / trampoline of every copied function (hooks_copies.cpp, read by its entry stub in hooks_stubs.cpp).
#define RSXR_FNEXT(id) extern "C" __declspec(dllexport) uintptr_t g_rsxr_fncopy_##id, g_rsxr_fntramp_##id;
RSXR_FNEXT(GSFR)
RSXR_FNEXT(GSBD)
RSXR_FNEXT(GSUL)
RSXR_FNEXT(GSLD)
RSXR_FNEXT(GSFO)
RSXR_FNEXT(GSFI)
RSXR_FNEXT(GSXF)
RSXR_FNEXT(GSWD)
RSXR_FNEXT(GSUP)
RSXR_FNEXT(GSRS)
RSXR_FNEXT(GSPK)
RSXR_FNEXT(GSVB)
RSXR_FNEXT(GSIC)
RSXR_FNEXT(GSDM)
RSXR_FNEXT(GSGL)
RSXR_FNEXT(GSHC)
#undef RSXR_FNEXT

// The extra palette of strings 6/7 (non-native colours, filled by hooks_copies.cpp FillPalette): the RGB of
// g_rsxr_ns_rgb[mode][s - 6][state] (state 0 lit, 1 dimmed, 2 glow).
inline float* RsxrGs_NsRgb(uint32_t mode, uint32_t s, uint32_t state) {
    return g_rsxr_ns_rgb + ((mode * 2 + (s - 6)) * 3 + state) * 3;
}

extern "C" {

// Mid-hook callbacks (hooks_entries.cpp: the GS<xx> entries, GRM1, GRM2; hooks_mid.cpp: the rest).
#define RSXR_GS_MID_DECL(ID) __declspec(dllexport) void __cdecl Rsxr_Mid_##ID(RsxrMid::Context&);
// clang-format off
RSXR_GS_MID_DECL(GSFR) RSXR_GS_MID_DECL(GSBD) RSXR_GS_MID_DECL(GSUL) RSXR_GS_MID_DECL(GSLD) RSXR_GS_MID_DECL(GSFO)
RSXR_GS_MID_DECL(GSFI) RSXR_GS_MID_DECL(GSXF) RSXR_GS_MID_DECL(GSWD) RSXR_GS_MID_DECL(GSUP) RSXR_GS_MID_DECL(GSRS)
RSXR_GS_MID_DECL(GSPK) RSXR_GS_MID_DECL(GSVB) RSXR_GS_MID_DECL(GSIC) RSXR_GS_MID_DECL(GSDM) RSXR_GS_MID_DECL(GSGL)
RSXR_GS_MID_DECL(GSHC) RSXR_GS_MID_DECL(GRM1) RSXR_GS_MID_DECL(GRM2)
// clang-format on
#undef RSXR_GS_MID_DECL
                __declspec(dllexport) void __cdecl Rsxr_Mid_GSZ1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GIZ1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GSSS(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GSSE(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GSA1(RsxrMid::Context&);  // call site, Before
__declspec(dllexport) void __cdecl Rsxr_Mid_GRM3(RsxrMid::Context&);
// GIA1, GSC1: Wrap sites (hooks_wrap.cpp).
__declspec(dllexport) void __cdecl Rsxr_Mid_GSC1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GIA1Before(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GIA1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_GIP1(RsxrMid::Context&);  // FromProfile, After
__declspec(dllexport) extern uintptr_t g_rsxr_target_GIP1;            // hooks_wrap.cpp, bound by Rsxr_Startup

// Link variables of the sites (hooks_mid.cpp, GRM1 / GRM2: hooks_entries.cpp), bound by Rsxr_Startup from the sites'
// resume / target.
__declspec(dllexport) extern uintptr_t g_rsxr_target_GSA1, g_rsxr_resume_GRM1, g_rsxr_resume_GRM2, g_rsxr_resume_GRM3,
    g_rsxr_resume_GSZ1, g_rsxr_resume_GIZ1, g_rsxr_resume_GSSS, g_rsxr_target_GSSS, g_rsxr_target_GSSE;
}

// GIA1's frame: the allocator 0x8F9D40 (size, flags; cdecl), the size argument edited by the before callback.
inline constexpr RsxrMid::Wrap kRsxrWrapGIA1{2, RsxrMid::Conv::Cdecl};
// GSC1's frame: Rnd_GuitarStrings_Ctor 0x599CE0 (the allocation; `ret 4`), run by the entry with the game's x87 state.
inline constexpr RsxrMid::Wrap kRsxrWrapGSC1{1, RsxrMid::Conv::Stdcall};

namespace rsxrgs {
constexpr int kMaxInfos = 64;

// The feature's state (g_strings, hooks.cpp). Counters and log caps are interlocked: the owners run on resource-loader
// jobs, the frame on the game thread.
struct State {
    // Native InstrumentInfos (allocated by GIA1 with kInfoSize): a fixed set, lock-free.
    uint8_t* volatile infos[kMaxInfos];
    // RsxrGs_Stats.
    volatile long native, active7, raised, lowered;
    // Log once / a few times.
    volatile long logCtor, logNoInfo, logUse[kGsKinds], logRaise, logLower, logAttach, logShape, logLoad,
        logReject[kGsKinds];
};

extern State g_strings;

// GIA1 reserves tracking before requesting an enlarged allocation. A zero token uses the stock size;
// FinishInfo releases the reservation on allocation failure, or registers the native allocation.
uint32_t ReserveInfo();
void FinishInfo(uint32_t token, uint8_t* info);
// Construction and the later profile assignment: incomplete extras or an untracked info keep stock counts.
void RefreshBuildCounts(uint8_t* gs, uint8_t* info);

inline uint8_t* Entity(uint8_t* obj) {
    return obj ? UncheckedRead<uint8_t*>(obj, 0x04) : nullptr;
}

// Calls into the game (hooks_game.cpp).
uint8_t* GameAlloc(uint32_t size);
void GameStrCtor(uint8_t* obj, uint32_t kind);  // Rnd_GuitarString_Ctor(ESI = this, EAX = kind)
void GameShow(uint8_t* entity, uint32_t show);  // EAX = entity, cdecl(show)
void GameDetach(uint8_t* entity, uint32_t detach);
// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 (EAX entity; mgr, seconds, 0, 1) ret 10h.
void GameFade(uint8_t* entity, bool in, float seconds);
}  // namespace rsxrgs

// The call-in entries the copies' calls need beyond their own (hooks_callin.cpp), before the copies are built.
bool RsxrGs_PrepareCallIns();
