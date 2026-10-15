#pragma once
#include <cstdint>

// Stage-5 publication (a native 7-string unit member): all five native layouts install the ports and
// publishes the song's string count (7/8) in InstrumentInfo +0Bh at song setup (publish_state).
namespace rs7pub {
constexpr unsigned kPrerequisites = 31;  // strings, fretgrid, nextgrid, pitch, colours
unsigned Missing(unsigned mask);
bool PortsEnabled(bool requested, unsigned mask);
bool PublicationReady();
// Exact scalar form of 875240..8753D2: candidate order and early exit match the stock unroll.
// Only guitar; count must be 6, 7 or 8. Writes record +14h (fret), +18h (string) on a match.
int Classify(uint8_t* record, const int16_t* tuning, unsigned count, int midi, int initial);
}  // namespace rs7pub

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_publish_ports;
__declspec(dllexport) extern uintptr_t g_rs7_native_publish;
__declspec(dllexport) void __cdecl Rs7Publish_Configure(int requested);
__declspec(dllexport) uintptr_t __cdecl Rs7Publish_Target(unsigned kind, uint8_t* reg);
__declspec(dllexport) int __cdecl Rs7Publish_Classify(uint8_t* tracker, uint8_t* record, int midi, int initial);
}
bool Rs7Publish_BuildCopies();
