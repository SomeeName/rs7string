#pragma once
#include "features/headstock/headstock.h"
#include "platform/midhook.h"
#include <cstdint>
#include <intrin.h>

// The headstock's game glue inside the feature: the refs and link variables of its sites, the entry points the stubs
// call, the game calls of the PegSet glue and the debug experiments. Not for code outside src/features/headstock/.

extern "C" {
// Refs (group Core, registration.cpp), bound by Rs7_Startup before any site is patched.
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_euler;  // X_EULER 0x8FCAA0 ESI = &rotate 3x3, EDI = &e[2], (&e[0], &e[1]) ret 8
__declspec(dllexport) extern uintptr_t g_rs7_fn_pegplace;   // X_PEGPLACE 0x59C630 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rs7_fn_peglayer;   // X_PEGLAYER 0x59C6E0 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rs7_pegnames;      // X_PEGNAMES the six colour names 0x12ED0C0
__declspec(dllexport) extern uintptr_t g_rs7_fn_pegselect;  // X_PEGSELECT 0x5B0880 ECX = session, (index) ret 4
__declspec(dllexport) extern uintptr_t g_rs7_peg_load;      // PSL1: slots a 6-string guitar loads (7)

// The log lines of the PegSet glue (exported: the emulator tests replace it with `ret`).
__declspec(dllexport) void __cdecl Rs7_HeadLog(const char* fmt, const void* a, const void* b, const void* c);
// What the PS* stubs and mid-hooks call (hooks_pegset.cpp).
__declspec(dllexport) void __cdecl Rs7_PegSetCtor(uint8_t* pegset);                 // PSC1
__declspec(dllexport) void __cdecl Rs7_PegSetLoaded(uint8_t* pegset);               // PSL2
__declspec(dllexport) void __cdecl Rs7_PegNamed(uint8_t* session, uint32_t index);  // PSN2 (index >= 6)

// Mid-hook callbacks (hooks_mid.cpp; PGH1 in assetload_pegs.cpp).
__declspec(dllexport) void __cdecl Rs7_Mid_PSA2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSD1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSD2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSU3(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSC1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSL1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSL2(Rs7Mid::Context&);  // call site, After placement
__declspec(dllexport) void __cdecl Rs7_Mid_PSM2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSR1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSN1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PGH1(Rs7Mid::Context&);

__declspec(dllexport) void __cdecl Rs7_Mid_PSU1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSU2(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSM1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_LT1(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_AL1(Rs7Mid::Context&);

// PSN2, the named-colour dispatch 0x59BF80 (slot, `ret 4`): a Wrap site (hooks_wrap.cpp).
__declspec(dllexport) void __cdecl Rs7_Mid_PSN2Before(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_PSN2(Rs7Mid::Context&);

// Link variables of the sites (hooks_stubs.cpp), bound by Rs7_Startup from the sites' resume / target.
__declspec(dllexport) extern uintptr_t g_rs7_resume_PSA2, g_rs7_resume_PSD1, g_rs7_resume_PSD2, g_rs7_resume_PSU1,
    g_rs7_target_PSU1, g_rs7_resume_PSU2, g_rs7_target_PSU2, g_rs7_resume_PSU3, g_rs7_resume_PSM1, g_rs7_target_PSM1,
    g_rs7_resume_PSM2, g_rs7_target_PSM2, g_rs7_resume_PSL1, g_rs7_target_PSL2, g_rs7_resume_PSR1, g_rs7_resume_PSN1,
    g_rs7_resume_LT1, g_rs7_resume_AL1;
}

inline constexpr Rs7Mid::Wrap kRs7WrapPSN2{1, Rs7Mid::Conv::Stdcall};

// PSN1 sees selections for every string/player, including stock six-string tuner sessions.
void Rs7Head_TrackColourPlayer(uint8_t* session);
// Startup: the ini [debug] head_cull / head_stretch are re-read from (nullptr / empty = never).
void Rs7Head_SetIni(const wchar_t* path);

namespace rs7head {
// ---- game calls (hooks_game.cpp); each returns without a call when its function or object is missing

void GameShowEntity(uint8_t* entity, uint32_t show);  // 0x7AE370: EAX = entity, cdecl(show)
void GamePegSetCall(uintptr_t fn, uint8_t* pegset);   // 0x59C630 / 0x59C6E0: stdcall(PegSet)
// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 for one peg, as the PegSet fade loops call them.
void GameFade(uintptr_t fn, uint8_t* entity, float seconds);
// X_EULER of a 3x3 rotation, in degrees as Rnd_PegSet_ApplyTransforms 0x59C630 scales it (0x59C67D..0x59C6A5).
void GameEuler(const uint8_t* rot, float* e);
void GameSetVec(uintptr_t fn, uint8_t* entity, const float* v);  // X_SETPOS / X_SETROT: cdecl(entity, vec3)
void GameSetScaleU(uint8_t* entity, float k);                    // Entity_SetScale 0x7AE2C0: EAX = entity

// ---- the debug experiments (hooks_debug.cpp)

// Every N5 frame of a guitar built with 7/8 strings: [debug] head_cull / head_stretch, hot-reloaded.
void DebugFrame(uint8_t* guitar, uint8_t* head, uint8_t* pegset);

template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

// A log-once flag: true for the first call.
inline bool Once(volatile long* f) {
    return _InterlockedExchange(f, 1) == 0;
}
}  // namespace rs7head
