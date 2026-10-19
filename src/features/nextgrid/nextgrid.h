#pragma once
#include <cstdint>

struct Rs7HookTable;
constexpr int kRs7NgNative = 1;  // Rs7Hook::ng: written only with the native capacity-8 next-note grid
// The native next-note grid hooks (group Nextgrid): the NGB<va> layout sites, NG1, NGP, NG9; and the X_SPAWN /
// X_ADV3 refs NG9's row-6 spawn calls (data references, group Core).
extern const Rs7HookTable kRs7NextgridHooks;

// Native capacity-8 next-note / target grid (issue #18 stage 5 area 2, docs/ARCHITECTURE.md
// section 2), a member of the native 7-string unit (no ini switch since 1.0; the stage-1 R7b / N9 pair is gone).
//
// Rnd_NextNoteGrid is the stack object of Rnd_NeckTargets_Update 0x7E4870 (G = its base; Init 0x7AA0B0, Set 0x7AA140,
// Prune 0x7AA1B0, Register 0x7A5D50 only calls Set):
//   +0      pointer[26][26], row 68h (strings 6/7 already fit)
//   +0A90h  time[6][26] (stock only; rows 0-5)
//   +0D00h  u16 active string count, +0D02h u16 fret count; +0D04h..0D1Bh the time-set tree; (stock: /GS cookie)
//   +0D1Ch  NATIVE time[8][26], row 68h, 340h bytes; the grid is 105Ch bytes
// Frame of 0x7E4870: 0D34h -> 1074h (NGP probes the page it crosses); G = ebp-1060h, locals ebp-1064h..-1074h, the
// tree header ebp-35Ch / -358h / -34Ch. Every edit is in-place (sites NGB<va>, NG1, NGP, NG9; tools/gen_nextgrid.py).
// The active string count (InstrumentInfo +0Bh, passed to Init) is untouched, so strings 6/7 are stored, not
// published: the stock loops still cover rows 0-5 and NG9 spawns row 6 (Rs7_NeckRow6N).
// Logic: nextgrid.cpp (the switch). Glue: hooks.cpp (the row-6 neck targets of NG9), hooks_mid.cpp (NG1, NG9),
// hooks_stubs.cpp (NGP), registration.cpp (kRs7NextgridHooks; the NGB<va> rows come from sites.generated.inc,
// tools/gen_nextgrid.py).

//
// What the PUBLISH step (active string count 7 / 8 in InstrumentInfo +0Bh) still has to do for this area:
//  - nothing in Init / Set / Prune / Update: they read the count from the grid (+0D00h) / the local, so the stock
//    loops of 0x7E4870 and 0x7AA1B0 enumerate rows 6/7 by themselves. NG9 self-disables (Rs7_NeckRow6N returns -1
//    for a count above 6), so no double spawn; its tree-release half stays.
//  - the chord side of Register 0x7A5D50 (not edited here): NN1 chord loop bound `cmp cx,7` -> 8, NN3 / NN4 slide
//    guards `cmp byte [ebp-6],7` -> 8 (stage 2 sites), and the T8 / C8 chord arrays must hold 8 strings.
//  - Spawn 0x7A56F0 reads FretGrid cell (string, fret): string 6 is served by R1 (extrapolation) while
//    native_fretgrid=0; string 7 has no source then, so publish 8 needs native_fretgrid=1 (cells 6/7 + FGRB).

extern "C" {
// The native next-note grid is on (set by startup before any site is patched).
__declspec(dllexport) extern uintptr_t g_rs7_native_nextgrid;
}

namespace rs7ng {
constexpr uint32_t kRowBytes = 0x68;  // 26 cells of 4
constexpr uint32_t kCells = 26, kCapacity = 8, kStock = 6;
constexpr uint32_t kTimeStock = 0xA90, kTimeNative = 0xD1C;
constexpr uint32_t kTimeBytesStock = kStock * kRowBytes, kTimeBytesNative = kCapacity * kRowBytes;  // 270h, 340h
constexpr uint32_t kGridStock = 0xD1C, kGridNative = kTimeNative + kTimeBytesNative;                // D1Ch, 105Ch
constexpr uint32_t kFrameStock = 0xD34, kFrameNative = kFrameStock + (kGridNative - kGridStock);    // D34h, 1074h
constexpr uint32_t kSetEaxBias = kTimeNative / 4;  // NG1: EAX = s*26+f + 347h, stock `lea edi,[esi+eax*4]`

void Configure(bool native);  // startup and tests

// Pure: the byte offset of time[s][f] from G (native layout).
inline uint32_t TimeOff(uint32_t s, uint32_t f) {
    return kTimeNative + s * kRowBytes + f * 4;
}

// Pure: whether NG1 stores (s, f) at all: native Set accepts 8 strings and 26 frets.
inline bool SetAccepts(uint32_t s, uint32_t f) {
    return s < kCapacity && f < kCells;
}
}  // namespace rs7ng

// Neck targets for string 6 (hooks.cpp, called by NG9; docs/ARCHITECTURE.md, Q2).
//
// The next-note grid lives on the stack of Rnd_NeckTargets_Update 0x7E4870 (G = ebp-0xD20):
// notePtr[26][26] at G+0 (rows 6..25 zeroed by Init and never read by stock), time[6][26] at
// G+0xA90, std::set of note times with its header node at G+0xD04 (leftmost +0xD0C, size +0xD14).
// The native grid (above) keeps string 6's pointer in notePtr row 6 (G+0x270); NG9 prunes and spawns row
// 6 the way stock does rows 0-5 while the active string count is 6. (The stage-1 R7b / N9 pair, which kept the time
// row in the free notePtr row 25 of the stock layout, was removed in 1.0.)
namespace rs7neck {
constexpr uint32_t kRow6Ptr = 0x270;   // notePtr[6][0]
constexpr uint32_t kSetHead = 0xD04;   // set header node (end())
constexpr uint32_t kSetBegin = 0xD0C;  // leftmost node (begin())
constexpr uint32_t kSetSize = 0xD14;
constexpr uint32_t kNodeTime = 0x10;  // float key of a set node
constexpr uint32_t kRowCells = 26;
// The native layout (above): time[8][26] sits behind the tree at G+0xD1C, so string 6's times
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
