#pragma once
#include "features/strings/strings.h"
#include "platform/midhook.h"
#include <cstdint>
#include <intrin.h>

// The native GuitarStrings' game glue inside the feature: the copies' variables, the link variables of the sites,
// the stubs and mid-hook callbacks registration.cpp lists, the feature's state and its calls into the game. Not
// for code outside src/features/strings/.

// The copy / trampoline of every copied function (hooks_copies.cpp, read by its entry stub in hooks_stubs.cpp).
#define RS7_FNEXT(id) extern "C" __declspec(dllexport) uintptr_t g_rs7_fncopy_##id, g_rs7_fntramp_##id;
RS7_FNEXT(GSFR)
RS7_FNEXT(GSBD)
RS7_FNEXT(GSUL)
RS7_FNEXT(GSLD)
RS7_FNEXT(GSFO)
RS7_FNEXT(GSFI)
RS7_FNEXT(GSXF)
RS7_FNEXT(GSWD)
RS7_FNEXT(GSUP)
RS7_FNEXT(GSRS)
RS7_FNEXT(GSPK)
RS7_FNEXT(GSVB)
RS7_FNEXT(GSIC)
RS7_FNEXT(GSDM)
RS7_FNEXT(GSGL)
RS7_FNEXT(GSHC)
#undef RS7_FNEXT

extern "C" {

// Mid-hook callbacks (hooks_entries.cpp: the GS<xx> entries, GRM1, GRM2; hooks_mid.cpp: the rest).
#define RS7_GS_MID_DECL(ID) __declspec(dllexport) void __cdecl Rs7_Mid_##ID(Rs7Mid::Context&);
// clang-format off
RS7_GS_MID_DECL(GSFR) RS7_GS_MID_DECL(GSBD) RS7_GS_MID_DECL(GSUL) RS7_GS_MID_DECL(GSLD) RS7_GS_MID_DECL(GSFO)
RS7_GS_MID_DECL(GSFI) RS7_GS_MID_DECL(GSXF) RS7_GS_MID_DECL(GSWD) RS7_GS_MID_DECL(GSUP) RS7_GS_MID_DECL(GSRS)
RS7_GS_MID_DECL(GSPK) RS7_GS_MID_DECL(GSVB) RS7_GS_MID_DECL(GSIC) RS7_GS_MID_DECL(GSDM) RS7_GS_MID_DECL(GSGL)
RS7_GS_MID_DECL(GSHC) RS7_GS_MID_DECL(GRM1) RS7_GS_MID_DECL(GRM2)
// clang-format on
#undef RS7_GS_MID_DECL
                __declspec(dllexport) void __cdecl Rs7_Mid_GSZ1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GIZ1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSSS(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSSE(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSA1(Rs7Mid::Context&);  // call site, Before
__declspec(dllexport) void __cdecl Rs7_Mid_GRM3(Rs7Mid::Context&);
// GIA1, GSC1: Wrap sites (hooks_wrap.cpp).
__declspec(dllexport) void __cdecl Rs7_Mid_GSC1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GIA1Before(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GIA1(Rs7Mid::Context&);

// Link variables of the sites (hooks_mid.cpp, GRM1 / GRM2: hooks_entries.cpp), bound by Rs7_Startup from the sites'
// resume / target.
__declspec(dllexport) extern uintptr_t g_rs7_target_GSA1, g_rs7_resume_GRM1, g_rs7_resume_GRM2, g_rs7_resume_GRM3,
    g_rs7_resume_GSZ1, g_rs7_resume_GIZ1, g_rs7_resume_GSSS, g_rs7_target_GSSS, g_rs7_target_GSSE;
}

// GIA1's frame: the allocator 0x8F9D40 (size, flags; cdecl), the size argument edited by the before callback.
inline constexpr Rs7Mid::Wrap kRs7WrapGIA1{2, Rs7Mid::Conv::Cdecl};
// GSC1's frame: Rnd_GuitarStrings_Ctor 0x599CE0 (the allocation; `ret 4`), run by the entry with the game's x87 state.
inline constexpr Rs7Mid::Wrap kRs7WrapGSC1{1, Rs7Mid::Conv::Stdcall};

namespace rs7gs {
constexpr int kMaxInfos = 64;

// The feature's state (g_strings, hooks.cpp). Counters and log caps are interlocked: the owners run on resource-loader
// jobs, the frame on the game thread.
struct State {
    // Native InstrumentInfos (allocated by GIA1 with kInfoSize): a fixed set, lock-free.
    uint8_t* volatile infos[kMaxInfos];
    // Rs7Gs_Stats.
    volatile long native, active7, raised, lowered;
    // Log once / a few times.
    volatile long logCtor, logNoInfo, logUse[kGsKinds], logRaise, logLower, logAttach, logShape, logLoad,
        logReject[kGsKinds];
};

extern State g_strings;

inline bool Upto(volatile long* c, long cap) {
    return _InterlockedIncrement(c) <= cap;
}

inline uint8_t* Entity(uint8_t* obj) {
    return obj ? Rd<uint8_t*>(obj, 0x04) : nullptr;
}

// Calls into the game (hooks_game.cpp).
uint8_t* GameAlloc(uint32_t size);
void GameStrCtor(uint8_t* obj, uint32_t kind);  // Rnd_GuitarString_Ctor(ESI = this, EAX = kind)
void GameShow(uint8_t* entity, uint32_t show);  // EAX = entity, cdecl(show)
void GameDetach(uint8_t* entity, uint32_t detach);
// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 (EAX entity; mgr, seconds, 0, 1) ret 10h.
void GameFade(uint8_t* entity, bool in, float seconds);
}  // namespace rs7gs

// The call-in entries the copies' calls need beyond their own (hooks_callin.cpp), before the copies are built.
bool Rs7Gs_PrepareCallIns();
