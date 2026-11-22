#pragma once
#include "features/headstock/headstock.h"
#include "platform/field.h"
#include "platform/midhook.h"
#include <cstdint>
#include <intrin.h>

// The headstock's game glue inside the feature: the refs and link variables of its sites, the entry points the stubs
// call, the game calls of the PegSet glue and the debug experiment. Not for code outside src/features/headstock/.

extern "C" {
// Refs (group Core, registration.cpp), bound by Rsxr_Startup before any site is patched.
__declspec(dllexport) extern uintptr_t
    g_rsxr_fn_euler;  // X_EULER 0x8FCAA0 ESI = &rotate 3x3, EDI = &e[2], (&e[0], &e[1]) ret 8
__declspec(dllexport) extern uintptr_t g_rsxr_fn_pegplace;   // X_PEGPLACE 0x59C630 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rsxr_fn_peglayer;   // X_PEGLAYER 0x59C6E0 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rsxr_pegnames;      // X_PEGNAMES the six colour names 0x12ED0C0
__declspec(dllexport) extern uintptr_t g_rsxr_fn_pegselect;  // X_PEGSELECT 0x5B0880 ECX = session, (index) ret 4
__declspec(dllexport) extern uintptr_t g_rsxr_peg_load;      // PSL1: slots a 6-string guitar loads (7)

// The log lines of the PegSet glue (exported: the emulator tests replace it with `ret`).
__declspec(dllexport) void __cdecl Rsxr_HeadLog(const char* fmt, const void* a, const void* b, const void* c);
// What the PS* stubs and mid-hooks call (hooks_pegset.cpp).
__declspec(dllexport) void __cdecl Rsxr_PegSetCtor(uint8_t* pegset);                 // PSC1
__declspec(dllexport) void __cdecl Rsxr_PegSetLoaded(uint8_t* pegset);               // PSL2
__declspec(dllexport) void __cdecl Rsxr_PegNamed(uint8_t* session, uint32_t index);  // PSN2 (index >= 6)

// Mid-hook callbacks (hooks_mid.cpp; PGH1 in assetload_pegs.cpp).
__declspec(dllexport) void __cdecl Rsxr_Mid_PSA2(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSD1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSD2(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSU3(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSC1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSL1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSL2(RsxrMid::Context&);  // call site, After placement
__declspec(dllexport) void __cdecl Rsxr_Mid_PSM2(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSR1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PGH1(RsxrMid::Context&);

__declspec(dllexport) void __cdecl Rsxr_Mid_PSU1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSU2(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSM1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_LT0(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_LT1(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_AL1(RsxrMid::Context&);

// PSN2, the named-colour dispatch 0x59BF80 (slot, `ret 4`): a Wrap site (hooks_wrap.cpp).
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN2Before(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN3Before(RsxrMid::Context&);
__declspec(dllexport) void __cdecl Rsxr_Mid_PSN2(RsxrMid::Context&);

// Link variables of the sites (hooks_stubs.cpp), bound by Rsxr_Startup from the sites' resume / target.
__declspec(dllexport) extern uintptr_t g_rsxr_resume_PSA2, g_rsxr_resume_PSD1, g_rsxr_resume_PSD2, g_rsxr_resume_PSU1,
    g_rsxr_target_PSU1, g_rsxr_resume_PSU2, g_rsxr_target_PSU2, g_rsxr_resume_PSU3, g_rsxr_resume_PSM1,
    g_rsxr_target_PSM1, g_rsxr_resume_PSM2, g_rsxr_target_PSM2, g_rsxr_resume_PSL1, g_rsxr_target_PSL2,
    g_rsxr_resume_PSR1, g_rsxr_resume_PSN1, g_rsxr_resume_LT1, g_rsxr_resume_AL1;
}

inline constexpr RsxrMid::Wrap kRsxrWrapPSN2{1, RsxrMid::Conv::Stdcall};
inline constexpr RsxrMid::Wrap kRsxrWrapPSN3{1, RsxrMid::Conv::Stdcall};  // the same call at 0x5B09D2

// PSN1 sees selections for every string/player, including stock six-string tuner sessions.
void RsxrHead_TrackColourPlayer(uint8_t* session);
// Startup: the ini [debug] head_cull is re-read from (nullptr / empty = never).
void RsxrHead_SetIni(const wchar_t* path);

namespace rsxrhead {
// ---- game calls (hooks_game.cpp); each returns without a call when its function or object is missing

void GameShowEntity(uint8_t* entity, uint32_t show);  // 0x7AE370: EAX = entity, cdecl(show)
void GamePegSetCall(uintptr_t fn, uint8_t* pegset);   // 0x59C630 / 0x59C6E0: stdcall(PegSet)
// ClipFade_FadeInEntity 0x824E70 / FadeOutEntity 0x825000 for one peg, as the PegSet fade loops call them.
void GameFade(uintptr_t fn, uint8_t* entity, float seconds);
// What Rnd_PegSet_ApplyTransforms 0x59C630 does per slot, over the first `count` slots: every taken slot's peg takes
// its locator's world position and rotation (X_EULER, X_SETPOS, X_SETROT). SEH-guarded; false on a fault or without
// the PegSet or a game function.
bool GamePlacePegs(uint8_t* pegset, unsigned count);

// ---- the debug experiment (hooks_debug.cpp)

// Every N5 frame of a guitar built with 7/8 strings: [debug] head_cull, hot-reloaded.
void DebugFrame(uint8_t* head);

using rsxrmem::UncheckedField;
}  // namespace rsxrhead
