#pragma once
// The N-slot chord code's game glue shared inside the feature (not for code outside src/features/nslot/): the frames of
// its Wrap sites (ARCHITECTURE.md section 14 "Wrap placement"), declared once for the hook rows (registration.cpp) and
// the callbacks (hooks_wrap.cpp).
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <cstdint>

// PP1: SNG_Song_PostProcess 0x559FD0 (song, second argument; `ret 8`).
inline constexpr RsxrMid::Wrap kRsxrWrapPP1{2, RsxrMid::Conv::Stdcall};
// AM1: 0x4E6A70 (targets, tolerance, two time floats; `ret 10h`), with an int32[8] in the scratch.
inline constexpr RsxrMid::Wrap kRsxrWrapAM1{4, RsxrMid::Conv::Stdcall, 32};
// MD1: the entry of Play_Matcher_DetectChord 0x4E7B30 (matcher, targets, tolerance, history; `ret 10h`), with the
// compacted int32[6] in the scratch.
inline constexpr RsxrMid::Wrap kRsxrWrapMD1{4, RsxrMid::Conv::Stdcall, 32};

extern "C" {
__declspec(dllexport) void __cdecl Rsxr_Mid_PP1(RsxrMid::Context& c);
__declspec(dllexport) void __cdecl Rsxr_Mid_AM1Before(RsxrMid::Context& c);
__declspec(dllexport) void __cdecl Rsxr_Mid_MD1Before(RsxrMid::Context& c);
__declspec(dllexport) void __cdecl Rsxr_Mid_XB1(RsxrMid::Context& c);  // hooks_extblock.cpp
}

// The callbacks, stubs and resume / target slots of the other sites, for the hook rows (registration.cpp). Their
// defining files do not include this header.
extern "C" {
// hooks_records.cpp, hooks_eval.cpp, hooks_latches.cpp
void Rsxr_Mid_TP1(RsxrMid::Context&);
void Rsxr_Mid_TP2(RsxrMid::Context&);
void Rsxr_Mid_TP3(RsxrMid::Context&);
void Rsxr_Mid_TP4(RsxrMid::Context&);
void Rsxr_Mid_TP5(RsxrMid::Context&);
void Rsxr_Mid_TP6(RsxrMid::Context&);
void Rsxr_Mid_TP7(RsxrMid::Context&);
void Rsxr_Mid_TP8(RsxrMid::Context&);
void Rsxr_Mid_TP12(RsxrMid::Context&);
void Rsxr_Mid_CP1(RsxrMid::Context&);
void Rsxr_Mid_CP2(RsxrMid::Context&);
void Rsxr_Mid_LU3(RsxrMid::Context&);
void Rsxr_Mid_WC2(RsxrMid::Context&);
void Rsxr_Mid_CS1(RsxrMid::Context&);
void Rsxr_Mid_CC3(RsxrMid::Context&);
void Rsxr_Mid_CH3(RsxrMid::Context&);
void Rsxr_Mid_UP1(RsxrMid::Context&);
void Rsxr_Mid_UP2(RsxrMid::Context&);
void Rsxr_Mid_UP3(RsxrMid::Context&);
void Rsxr_Mid_PD2(RsxrMid::Context&);
void Rsxr_Mid_PD3(RsxrMid::Context&);
void Rsxr_Mid_PD4(RsxrMid::Context&);
void Rsxr_Mid_PD5(RsxrMid::Context&);
void Rsxr_Mid_TP11(RsxrMid::Context&);
void Rsxr_Mid_DS1(RsxrMid::Context&);
void Rsxr_Mid_PL1(RsxrMid::Context&);
void Rsxr_Mid_SF1(RsxrMid::Context&);  // hooks_free.cpp
void Rsxr_Mid_TM(RsxrMid::Context&);   // hooks_calls.cpp: TM1-TM6
void Rsxr_Mid_EV(RsxrMid::Context&);   // EV1-EV5
extern uintptr_t g_rsxr_target_TM1, g_rsxr_target_TM2, g_rsxr_target_TM3, g_rsxr_target_TM4, g_rsxr_target_TM5,
    g_rsxr_target_TM6, g_rsxr_target_EV1, g_rsxr_target_EV2, g_rsxr_target_EV3, g_rsxr_target_EV4, g_rsxr_target_EV5;
extern uintptr_t g_rsxr_resume_DS1;
extern uintptr_t g_rsxr_resume_CC3;
extern uintptr_t g_rsxr_resume_CH3;
extern uintptr_t g_rsxr_fn_evrel;
void Rsxr_Stub_RL1();
extern uintptr_t g_rsxr_resume_RL1;
void Rsxr_Stub_RL2();
extern uintptr_t g_rsxr_resume_RL2;
void Rsxr_Mid_AG1(RsxrMid::Context&);  // hooks_eval.cpp: AG1-AG3
void Rsxr_Mid_AG2(RsxrMid::Context&);
void Rsxr_Mid_AG3(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_UP1, g_rsxr_target_UP1;
extern uintptr_t g_rsxr_resume_UP2, g_rsxr_target_UP2;
extern uintptr_t g_rsxr_resume_UP3, g_rsxr_target_UP3;
extern uintptr_t g_rsxr_resume_PD2, g_rsxr_target_PD2;
extern uintptr_t g_rsxr_resume_PD3, g_rsxr_target_PD3;
extern uintptr_t g_rsxr_resume_PD4, g_rsxr_target_PD4;
extern uintptr_t g_rsxr_resume_PD5, g_rsxr_target_PD5;
extern uintptr_t g_rsxr_resume_TP12;
extern uintptr_t g_rsxr_resume_PL1;
extern uintptr_t g_rsxr_resume_LU3;
extern uintptr_t g_rsxr_resume_WC2;
void Rsxr_Mid_BT1(RsxrMid::Context&);  // hooks_eval.cpp
extern uintptr_t g_rsxr_resume_CS1;
extern uintptr_t g_rsxr_resume_TP1, g_rsxr_resume_TP2, g_rsxr_resume_TP3, g_rsxr_resume_TP4, g_rsxr_resume_TP5,
    g_rsxr_resume_TP6, g_rsxr_resume_TP7, g_rsxr_resume_TP8, g_rsxr_resume_TP11, g_rsxr_resume_CP1, g_rsxr_resume_CP2,
    g_rsxr_target_SF1;
}

namespace rsxrnslot {
// SNG_Song: vectors of templates, ChordNotes, tuning (int16) and levels (stride kLevelSize); the capo byte.
constexpr uint32_t kSongTemplates = 0x94, kSongChordNotes = 0xAC, kSongTuning = 0x110, kSongLevels = 0x40;
constexpr uint32_t kSongCapo = 0x11C;
// A level: its note vector (stride kNoteSize). A note: u32 mask +0, int32 chord id +14h, int32 ChordNotes id +18h.
constexpr uint32_t kLevelSize = 100, kLevelNotes = 0x30;
constexpr uint32_t kNoteSize = 0x1C8, kNoteMask = 0, kNoteChordIds = 0x14;

// A game vector's begin / end pointers at `off` in the song object (false: unreadable, or end before begin).
struct Vec {
    uintptr_t begin = 0, end = 0;
};

inline bool ReadVec(const void* song, uint32_t off, Vec* v) {
    return Rsxr_SafeCopy(v, static_cast<const uint8_t*>(song) + off, sizeof *v) && v->end >= v->begin;
}
}  // namespace rsxrnslot
