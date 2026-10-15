#pragma once
#include <cstdint>
bool Rs7PublishPitch_BuildCopies();
extern "C" {
__declspec(dllexport) uintptr_t __cdecl Rs7PublishPitch_Target(unsigned kind, uint8_t* object);
__declspec(dllexport) void __cdecl Rs7PublishPitch_ChordTail(int32_t* out8, const uint8_t* tpl, const uint8_t* tracker);
__declspec(dllexport) extern const int16_t g_rs7_publish_zero8[8];
__declspec(dllexport) void Rs7_Publish_ZeroRaw();
}
