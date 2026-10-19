#pragma once
#include "features/frets/frets.h"
#include "platform/midhook.h"
#include <cstdint>

// The Wrap frames of F1 (Rnd_FretBars_ApplyTransforms: no stack arguments, plain `ret`) and IN1
// (Rnd_Inlays_ApplyTransforms: the inlay set, `ret 4`), for registration.cpp and hooks_wrap.cpp.
inline constexpr Rs7Mid::Wrap kRs7WrapF1{0, Rs7Mid::Conv::Stdcall};
inline constexpr Rs7Mid::Wrap kRs7WrapIN1{1, Rs7Mid::Conv::Stdcall};

extern "C" {
__declspec(dllexport) void __cdecl Rs7_Mid_F1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_IN1(Rs7Mid::Context& c);
}

// The fret bars' game glue shared by hooks.cpp and hooks_log.cpp (internal: other code includes
// features/frets/frets.h).
namespace rs7fretglue {
template <class T>
T& At(uint8_t* base, uint32_t off) {
    return *reinterpret_cast<T*>(base + off);
}

// Fret bar f of a FretGrid (+5C40h, 38h each).
uint8_t* Bar(uint8_t* grid, int f);
// The guitar's fret bar count (info +0Dh), at most kMaxBars; 0 without an info.
int BarCount(uint8_t* guitar);
// String 0/1 attach points (local, GuitarStrings stringObj[0/1] +8) and world positions (+14h; w0 / w1 may be null).
// False while the guitar's strings are not loaded.
bool Attach(uint8_t* guitar, const float** a0, const float** a1, const float** w0, const float** w1);

// The log lines (hooks_log.cpp). None throws.
void LogApply(const char* by, const uint8_t* guitar, const uint8_t* grid, int bars, int set, const rs7frets::Extend& x,
              const float* mid0, const float* new0);
void LogRestore(const uint8_t* guitar, int set);
void LogFail(const char* why, const uint8_t* guitar);
// Bars 0, 1 and 12 of the guitar: root node, first NiMesh, transforms and bounds along the strings.
void LogReadBack(const char* tag, uint8_t* guitar, uint8_t* grid);
void LogInlay(const uint8_t* guitar, const float* p, float dz);
void LogFloor(const uint8_t* guitar, const float* before, const float* shift, int n);
}  // namespace rs7fretglue
