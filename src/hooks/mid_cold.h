#pragma once
#include "hooks/midhook.h"

// C++ mid-hook callbacks that replaced hand-written naked stubs on cold paths (guitar / song load and
// free, session setup, teardown). docs/ARCHITECTURE.md, "Mid-hook conversion", lists the families and
// what stays hand-written. Defined in mid_cold.cpp; registered in registry.cpp.
extern "C" {
// Tuning names / tuner session / SNG load (batch 1)
__declspec(dllexport) void __cdecl Rs7_Mid_TK(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_TS(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_TUT(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_XB1(Rs7Mid::Context&);
// Pool request counts, GuitarStrings / InstrumentInfo free sizes, PegSet free size (batch 2)
__declspec(dllexport) void __cdecl Rs7_Mid_N1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_N21(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSZ1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GIZ1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSA2(Rs7Mid::Context&);
// Native PegSet destructor / unload / ctor / load (batch 3)
__declspec(dllexport) void __cdecl Rs7_Mid_PSD1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSD2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSU3(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSC1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSL1(Rs7Mid::Context&);
// Native PegSet / GuitarStrings loops with a conditional branch (batch 4)
__declspec(dllexport) void __cdecl Rs7_Mid_PSM2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSR1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSN1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSSS(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_GSSE(Rs7Mid::Context&);
// Legacy 7th string object load / free (batch 5)

// Continuations the callbacks above jump to (bound by Rs7_Startup from the sites' resume / target).
extern uintptr_t g_rs7_resume_N1, g_rs7_resume_N21, g_rs7_resume_GSZ1, g_rs7_resume_GIZ1, g_rs7_resume_PSA2,
    g_rs7_resume_PSD1, g_rs7_resume_PSD2, g_rs7_resume_PSU3, g_rs7_resume_PSL1, g_rs7_resume_PSM2, g_rs7_target_PSM2,
    g_rs7_resume_PSR1, g_rs7_resume_PSN1, g_rs7_resume_GSSS, g_rs7_target_GSSS, g_rs7_target_GSSE;
}
