#pragma once
#include <cstdint>

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_native_chordloops;
// Added strings only. Native owner, active/loaded slot and usable model are required.
// Uses GuitarStrings' own objects; does not mirror a stock string.
__declspec(dllexport) uint8_t* __cdecl Rs7ChordLoops_Target(uint8_t* owner, uint32_t s, uint32_t ghost);
}
