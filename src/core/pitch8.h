#pragma once
#include "core/mode.h"
#include <cstddef>
#include <cstdint>

// Native 8-entry tuning / open-MIDI storage of the detector and matcher (issue #18 stage 5 area 3,
// docs/ARCHITECTURE.md), a member of the native 7-string unit (no ini switch since 1.0; needs MSVCR100's _msize).
//
// Both objects keep every stock field; the stock code still writes and reads the six-entry arrays.
// The grown allocations (PA1/PA2) carry capacity-8 tails the DLL maintains next to the stock arrays
// (PR0 reset, S3/PT1 SetTuning exits, PM0 matcher ctor, PM1 matcher SetTuning), so slots 0..5 of a
// tail always equal the stock array. The expected-pitch ports (PE0 for 0x873E90, Rs7_Pitch_Raw8 for
// 0x873F80 via the PC1-PC4 call sites) read strings 6/7 from them. The active count stays 6 / 4
// (bass): this step publishes no new count.
//
// Detector (stock 13B8h -> 13E0h): int16 tuning8 +13B8h, int16 openMidi8 +13C8h, dword active count
// +13D8h, dword magic +13DCh. Matcher (stock 58h -> 6Ch): int16 tuning8 +58h, dword magic +68h.
// Tails are written and read only in blocks the CRT reports large enough (_msize), so an object
// allocated before PA1/PA2 were written is never overrun or read past; the magic marks a valid tail.
namespace rs7p {
constexpr uint32_t kDetTuning8 = 0x13B8;
constexpr uint32_t kDetOpen8 = 0x13C8;
constexpr uint32_t kDetCount = 0x13D8;
constexpr uint32_t kDetMagic = 0x13DC;
constexpr uint32_t kDetSize = 0x13E0;
constexpr uint32_t kDetStockSize = 0x13B8;
constexpr uint32_t kMatTuning8 = 0x58;
constexpr uint32_t kMatMagic = 0x68;
constexpr uint32_t kMatSize = 0x6C;
constexpr uint32_t kMatStockSize = 0x58;
constexpr uint32_t kMagic = 0x38503752;  // 'R7P8'
constexpr uint32_t kDetOpen6 = 0x134C;   // stock int16 openMidi[6]
}  // namespace rs7p

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_native_pitch;  // set before patching
// 1 while Play_Tuning_ApplyToDetection 0x5B63E0 runs (sites TS8PA / TS8PZ, [neck] native_tuner=1 only):
// it re-applies the PROFILE's six strings (e.g. around a tuner visit). A song's strings 6 / 7 (the last
// SetTuning with 7 / 8 entries on the same detector / matcher) then stay in the tails and in the
// lowest open MIDI instead of being zeroed. 0 (always with native_tuner=0): exactly as before.
__declspec(dllexport) extern uintptr_t g_rs7_profile_apply;
__declspec(dllexport) extern uintptr_t g_rs7_fn_msize;  // MSVCR100 _msize (the game's operator new heap)
// Base MIDI per string (the stock case constants of 0x873E90 / 0x873F80, plus B1 / F#1 for 6 / 7).
__declspec(dllexport) extern const int32_t g_rs7_pitch_gbase[8];
__declspec(dllexport) extern const int32_t g_rs7_pitch_bbase[4];

// Called from the stubs (cdecl, integer only - callers keep values in x87 registers; none throws).
__declspec(dllexport) void __cdecl Rs7Pitch_DetReset(uint8_t* det);  // PR0
// The string count a 7/8-string song widened the detector to at S4 (core/publish_state.h); 0 = stock. Set 0 clears.
// PR0 (a new / reset detector) clears it.
__declspec(dllexport) void __cdecl Rs7Pitch_SetSongCount(const uint8_t* det, unsigned count);
__declspec(dllexport) unsigned __cdecl Rs7Pitch_SongCount(const uint8_t* det);
__declspec(dllexport) void __cdecl Rs7Pitch_DetTuned(uint8_t* det, const Rs7Int16Vec* v);  // S3 / PT1
__declspec(dllexport) void __cdecl Rs7Pitch_MatNew(uint8_t* m);                            // PM0
__declspec(dllexport) void __cdecl Rs7Pitch_MatTuned(uint8_t* m, const Rs7Int16Vec* v);    // PM1
// Whether a profile re-apply keeps the song's extra strings (core/tuner8.cpp: native_tuner on).
int Rs7Pitch_KeepSongTuning();
// PC1-PC4 (strings >= 6 only): 1 = `obj` has a valid tail - its CRT block holds the native size
// (_msize, so nothing past a stock-size block is read) and carries the magic. kind 0 = matcher,
// 1 = detector.
__declspec(dllexport) int __cdecl Rs7Pitch_Tailed(uint8_t* obj, uint32_t kind);
// The 0x873F80 port (ECX = tuning, EDX = instrument, ESI = string, stack fret; caller pops 4).
__declspec(dllexport) void Rs7_Pitch_Raw8();
}

// Pure parts (unit tests).
namespace rs7p {
// tuning8 from a vector of n entries (n >= 0): the first min(n, 8), the rest 0.
void FillTuning(const int16_t* v, size_t n, int16_t out[8]);
// openMidi8 as the stock unrolled writers compute it, for all 8 slots: guitar base[s] + t[s]
// (int16 wrap like `add dx, imm`), bass base[s] + t[s] for s < 4 and the raw t[s] after.
void FillOpen(const int16_t t[8], bool bass, int16_t out[8]);
// The lowest open MIDI after SetTuning: stock (string 0) lowered to base[s] + t[s] (int32, as S3's
// 35 + t6) of strings 6..n-1 (guitar, n = entries, at most 8). Never raised; bass unchanged.
int Lowest(int stock, const int16_t t[8], size_t n, bool bass);
}  // namespace rs7p
