#pragma once
#include <cstdint>

// Neck targets for string 6 (docs/ARCHITECTURE.md, Q2).
//
// The next-note grid lives on the stack of Rnd_NeckTargets_Update 0x7E4870 (G = ebp-0xD20):
// notePtr[26][26] at G+0 (rows 6..25 zeroed by Init and never read by stock), time[6][26] at
// G+0xA90, std::set of note times with its header node at G+0xD04 (leftmost +0xD0C, size +0xD14).
// The native grid (core/nextgrid.h) keeps string 6's pointer in notePtr row 6 (G+0x270); NG9 prunes and spawns row
// 6 the way stock does rows 0-5 while the active string count is 6. (The stage-1 R7b / N9 pair, which kept the time
// row in the free notePtr row 25 of the stock layout, was removed in 1.0.)
namespace rs7neck {
constexpr uint32_t kRow6Ptr = 0x270;   // notePtr[6][0]
constexpr uint32_t kSetHead = 0xD04;   // set header node (end())
constexpr uint32_t kSetBegin = 0xD0C;  // leftmost node (begin())
constexpr uint32_t kSetSize = 0xD14;
constexpr uint32_t kNodeTime = 0x10;  // float key of a set node
constexpr uint32_t kRowCells = 26;
// The native layout (core/nextgrid.h): time[8][26] sits behind the tree at G+0xD1C, so string 6's times
// are time row 6 (G+0xD1C+6*0x68) and the pointer row 6 is the same G+0x270.
constexpr uint32_t kNgRow6Time = 0xD1C + 6 * 0x68;
constexpr uint32_t kStockStrings = 6;
}  // namespace rs7neck

// Bound by Rs7_Startup from callref sites (never written): Rnd_NeckTarget_Spawn 0x7A56F0 (X_SPAWN,
// stdcall 6 args, ret 18h) and Rnd_TimeSet_Advance3 0x7AA880 (X_ADV3, ESI = &iterator, clobbers
// EAX/ECX/EDX).
extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_spawn;
extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_adv3;

extern "C" {
// NG9 (frame of 0x7E4870 grown by 340h): grid = ebp-0x1060, self = EBX (the renderer), dt/t = raw float bits,
// stringCount = [ebp-0x1074] (the active count). Row 6 empty (every non-7-string song): returns -1 having only read
// row 6. Otherwise prunes row 6 exactly like Rnd_NextNoteGrid_Prune 0x7AA1B0 prunes rows 0-5, then calls
// Rnd_NeckTarget_Spawn for every live row-6 cell like the stock consumer. Returns the first fret >= 1 it spawned,
// else -1. From 7 strings on the stock loops spawn row 6: returns -1. Pure (no Win32 calls, no exceptions).
__declspec(dllexport) int __cdecl Rs7_NeckRow6N(uint8_t* grid, void* self, uint32_t dtBits, uint32_t tBits,
                                                uint32_t fretCount, uint32_t stringCount);
// "neck: ..." log line, once per song (reset by Rs7Neck_ResetLog from song setup).
__declspec(dllexport) void __cdecl Rs7_NeckLog(int fret);
}
void Rs7Neck_ResetLog();
