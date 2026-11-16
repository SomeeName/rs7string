#pragma once
#include "features/frets/frets.h"
#include "platform/field.h"
#include "platform/midhook.h"
#include <cstdint>

// The Wrap frames of F1 (Rnd_FretBars_ApplyTransforms: no stack arguments, plain `ret`) and IN1
// (Rnd_Inlays_ApplyTransforms: the inlay set, `ret 4`), for registration.cpp and hooks_wrap.cpp.
inline constexpr Rs7Mid::Wrap kRs7WrapF1{0, Rs7Mid::Conv::Stdcall};
inline constexpr Rs7Mid::Wrap kRs7WrapIN1{1, Rs7Mid::Conv::Stdcall};

extern "C" {
__declspec(dllexport) void __cdecl Rs7_Mid_F1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_IN1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_SC1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_FRG1(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_FRG2(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_FRG3(Rs7Mid::Context& c);
}

// The fret bars' game glue shared by hooks.cpp and hooks_log.cpp (internal: other code includes
// features/frets/frets.h).
namespace rs7fretglue {
// The floor movement actually completed since the last stock transform (zero until ready).
int FloorMoved(const uint8_t* guitar);
// Stock local bar-B locator and actual attach points, shared by the floor and SC1. Same result as FloorMove;
// zero for stock instruments / frets off, -1 until the geometry is ready. Never reads the already moved world end.
int FloorOffset(uint8_t* guitar, uint8_t* grid, float out[3]);

using rs7mem::UncheckedField;

// Fret bar f of a FretGrid (+5C40h, 38h each).
uint8_t* Bar(uint8_t* grid, int f);
// The guitar's fret bar count (info +0Dh), at most kMaxBars; 0 without an info.
int BarCount(uint8_t* guitar);
// String 0/1 attach points (local, GuitarStrings stringObj[0/1] +8) and world positions (+14h; w0 / w1 may be null).
// False while the guitar's strings are not loaded.
bool Attach(uint8_t* guitar, const float** a0, const float** a1, const float** w0, const float** w1);
// Lanes beyond the stock low string (7/8-string guitar: 1 / 2, extended bass: 1), the stock far string (bass 3, guitar
// 5) and the lanes beyond it (a 6-string bass's high C: 1).
int ExtraLanes(const uint8_t* guitar);
int StockFar(const uint8_t* guitar);
int FarLanes(const uint8_t* guitar);
// Entity_SetScale 0x7AE2C0.
void GameSetScale(uint8_t* entity, float k);
// The glow bars FRG2 has not scaled yet and the glow logs, per song (hooks_glow.cpp).
void ResetGlow();

// The log lines (hooks_log.cpp). None throws.
void LogApply(const char* by, const uint8_t* guitar, const uint8_t* grid, int bars, int set, const rs7frets::Extend& x,
              const float* mid0, const float* new0);
void LogRestore(const uint8_t* guitar, int set);
void LogFail(const char* why, const uint8_t* guitar);
// Bars 0, 1 and 12 of the guitar: root node, first NiMesh, transforms and bounds along the strings.
void LogReadBack(const char* tag, uint8_t* guitar, uint8_t* grid);
void LogInlay(const uint8_t* guitar, const float* p, float dz, int net);
void LogFloor(const uint8_t* guitar, const float* before, const float* shift, int n);
}  // namespace rs7fretglue
