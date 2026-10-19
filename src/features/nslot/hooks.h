#pragma once
// The N-slot chord code's game glue shared inside the feature (not for code outside src/features/nslot/): the frames of
// its Wrap sites (ARCHITECTURE.md section 15 "Wrap placement"), declared once for the hook rows (registration.cpp) and
// the callbacks (hooks_wrap.cpp).
#include "platform/midhook.h"

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
}
