#pragma once
// The N-slot chord code's game glue shared inside the feature (not for code outside src/features/nslot/): the frames of
// its Wrap sites (ARCHITECTURE.md section 14 "Wrap placement"), declared once for the hook rows (registration.cpp) and
// the callbacks (hooks_wrap.cpp).
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <cstdint>

// PP1: SNG_Song_PostProcess 0x559FD0 (song, second argument; `ret 8`).
inline constexpr Rs7Mid::Wrap kRs7WrapPP1{2, Rs7Mid::Conv::Stdcall};
// AM1: 0x4E6A70 (targets, tolerance, two time floats; `ret 10h`), with an int32[8] in the scratch.
inline constexpr Rs7Mid::Wrap kRs7WrapAM1{4, Rs7Mid::Conv::Stdcall, 32};
// MD1: the entry of Play_Matcher_DetectChord 0x4E7B30 (matcher, targets, tolerance, history; `ret 10h`), with the
// compacted int32[6] in the scratch.
inline constexpr Rs7Mid::Wrap kRs7WrapMD1{4, Rs7Mid::Conv::Stdcall, 32};

extern "C" {
__declspec(dllexport) void __cdecl Rs7_Mid_PP1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_AM1Before(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_MD1Before(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_XB1(Rs7Mid::Context& c);  // hooks_extblock.cpp
}

// The callbacks, stubs and resume / target slots of the other sites, for the hook rows (registration.cpp). Their
// defining files do not include this header.
extern "C" {
// hooks_records.cpp, hooks_eval.cpp, hooks_latches.cpp
void Rs7_Mid_TP1(Rs7Mid::Context&);
void Rs7_Mid_TP2(Rs7Mid::Context&);
void Rs7_Mid_TP3(Rs7Mid::Context&);
void Rs7_Mid_TP4(Rs7Mid::Context&);
void Rs7_Mid_TP5(Rs7Mid::Context&);
void Rs7_Mid_TP6(Rs7Mid::Context&);
void Rs7_Mid_TP7(Rs7Mid::Context&);
void Rs7_Mid_TP8(Rs7Mid::Context&);
void Rs7_Mid_TP12(Rs7Mid::Context&);
void Rs7_Mid_CP1(Rs7Mid::Context&);
void Rs7_Mid_CP2(Rs7Mid::Context&);
void Rs7_Mid_LU3(Rs7Mid::Context&);
void Rs7_Mid_WC2(Rs7Mid::Context&);
void Rs7_Mid_CS1(Rs7Mid::Context&);
void Rs7_Mid_CC3(Rs7Mid::Context&);
void Rs7_Mid_CH3(Rs7Mid::Context&);
void Rs7_Mid_UP1(Rs7Mid::Context&);
void Rs7_Mid_UP2(Rs7Mid::Context&);
void Rs7_Mid_UP3(Rs7Mid::Context&);
void Rs7_Mid_PD2(Rs7Mid::Context&);
void Rs7_Mid_PD3(Rs7Mid::Context&);
void Rs7_Mid_PD4(Rs7Mid::Context&);
void Rs7_Mid_PD5(Rs7Mid::Context&);
void Rs7_Mid_TP11(Rs7Mid::Context&);
void Rs7_Mid_DS1(Rs7Mid::Context&);
void Rs7_Mid_PL1(Rs7Mid::Context&);
void Rs7_Mid_SF1(Rs7Mid::Context&);  // hooks_free.cpp
void Rs7_Mid_TM(Rs7Mid::Context&);   // hooks_calls.cpp: TM1-TM6
void Rs7_Mid_EV(Rs7Mid::Context&);   // EV1-EV5
extern uintptr_t g_rs7_target_TM1, g_rs7_target_TM2, g_rs7_target_TM3, g_rs7_target_TM4, g_rs7_target_TM5,
    g_rs7_target_TM6, g_rs7_target_EV1, g_rs7_target_EV2, g_rs7_target_EV3, g_rs7_target_EV4, g_rs7_target_EV5;
extern uintptr_t g_rs7_resume_DS1;
extern uintptr_t g_rs7_resume_CC3;
extern uintptr_t g_rs7_resume_CH3;
extern uintptr_t g_rs7_fn_evrel;
void Rs7_Stub_RL1();
extern uintptr_t g_rs7_resume_RL1;
void Rs7_Stub_RL2();
extern uintptr_t g_rs7_resume_RL2;
void Rs7_Mid_AG1(Rs7Mid::Context&);  // hooks_eval.cpp: AG1-AG3
void Rs7_Mid_AG2(Rs7Mid::Context&);
void Rs7_Mid_AG3(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_UP1, g_rs7_target_UP1;
extern uintptr_t g_rs7_resume_UP2, g_rs7_target_UP2;
extern uintptr_t g_rs7_resume_UP3, g_rs7_target_UP3;
extern uintptr_t g_rs7_resume_PD2, g_rs7_target_PD2;
extern uintptr_t g_rs7_resume_PD3, g_rs7_target_PD3;
extern uintptr_t g_rs7_resume_PD4, g_rs7_target_PD4;
extern uintptr_t g_rs7_resume_PD5, g_rs7_target_PD5;
extern uintptr_t g_rs7_resume_TP12;
extern uintptr_t g_rs7_resume_PL1;
extern uintptr_t g_rs7_resume_LU3;
extern uintptr_t g_rs7_resume_WC2;
void Rs7_Mid_BT1(Rs7Mid::Context&);  // hooks_eval.cpp
extern uintptr_t g_rs7_resume_CS1;
extern uintptr_t g_rs7_resume_TP1, g_rs7_resume_TP2, g_rs7_resume_TP3, g_rs7_resume_TP4, g_rs7_resume_TP5,
    g_rs7_resume_TP6, g_rs7_resume_TP7, g_rs7_resume_TP8, g_rs7_resume_TP11, g_rs7_resume_CP1, g_rs7_resume_CP2,
    g_rs7_target_SF1;
}

namespace rs7nslot {
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
    return Rs7_SafeCopy(v, static_cast<const uint8_t*>(song) + off, sizeof *v) && v->end >= v->begin;
}
}  // namespace rs7nslot
