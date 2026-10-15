#pragma once
#include "hooks/midhook.h"
#include <cstdint>

// Native 8-slot tuner (issue #18, job 3 "tuner8"; docs/ARCHITECTURE.md). A member of the native 7-string unit since
// 1.0 (no ini switch; g_rs7_native_tuner is cleared only when a startup check refuses it, and then nothing is patched).
//
// Needs the native strings, FretGrid and colours:
//  - Every UI_PlayerTuner is allocated with 380h bytes (TU8ALLOC) and registered (TU8INIT / TU8FREE);
//    the stock 278h prefix is unchanged.
//  - A session whose target tuning has 7 or 8 strings (TK: manifest keys Rs7String6 / Rs7String7; or
//    a published InstrumentInfo +0Bh of 7/8 with the detector's tuning8 tail, feat/publish) becomes a
//    NATIVE session at TS (Play_TunerSession_ApplyTuning): its six per-string arrays move to the
//    capacity-8 tail below (values copied, entity pointers moved), its string count N and its tuning
//    order (lowest string first: 7 strings = 6,0,1,2,3,4,5; 8 = 7,6,0,...,5) are stored there.
//  - The UI_PlayerTuner methods (TS8* sites, tools/gen_tuner8s.py) run as relocated copies for a
//    native session only: arrays from the tail, loops over N, steps through the order (Rs7Tu8_Advance).
//    Isolated reads in large functions are mid-hooks (TS8F*, TS8RS, TS8DT, TS8WR, TS8SN*).
//    Every other session (6-string guitar, bass, no key) runs the unmodified game code.
//  - The physical string index stays the game's (string 6 = the 7th string, its GuitarStrings slot,
//    peg 7, FretGrid cell 6, colour row 6): no slot is mirrored or re-targeted.
//  - The shared tuner hooks TK, TS, W and AT live in tuner.h (the legacy B-phase hooks were removed in 1.0).
// The GuitarStrings readers TU8H / TU8C / TU8A (HighlightString, ClearHighlight, StringAction) run
// their native copies for every native GuitarStrings (core/gsnative.h).

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_native_tuner;
__declspec(dllexport) int __cdecl Rs7Tuner8_Use(uint32_t kind, uint8_t* reg);
__declspec(dllexport) uintptr_t __cdecl Rs7Tuner8_Target(uint32_t kind, uint8_t* reg);
__declspec(dllexport) void Rs7_Stub_TU8H();
__declspec(dllexport) void Rs7_Stub_TU8C();
__declspec(dllexport) void Rs7_Stub_TU8A();
__declspec(dllexport) void __cdecl Rs7Tuner8_InitSession(uint8_t* session);
__declspec(dllexport) int __cdecl Rs7Tuner8_HasSession(const uint8_t* session);
__declspec(dllexport) uint32_t __cdecl Rs7Tuner8_FreeSessionSize(uint8_t* session);
__declspec(dllexport) void Rs7_Stub_TU8INIT();
__declspec(dllexport) void Rs7_Stub_TU8FREE();
__declspec(dllexport) extern uintptr_t g_rs7_resume_TU8FREE;

// Native sessions.
// 1 = `session` is a registered grown session in native mode (its tail holds the arrays).
__declspec(dllexport) int __cdecl Rs7Tuner8_IsNative(const uint8_t* session);
// The entry dispatch of a copied method: spec index, the PUSHAD frame of the stub (regs[0] = EDI ...
// regs[7] = EAX, regs[8] = the stub's slot, regs[9] = the caller's return address, regs[10] = arg 1).
__declspec(dllexport) uintptr_t __cdecl Rs7Tuner8_SessTarget(uint32_t index, const uint32_t* regs);
// Called by the copies. Rs7Tu8_Advance: stdcall (session, dir +1 / -1) -> EAX 1 = curString moved one
// step along the tuning order, 0 = no further string; preserves ECX / EDX. Rs7Tu8_OnSelect: cdecl
// (session, string), inside PUSHAD. Rs7Tu8_First: stdcall (session) -> EAX the first string of the order.
__declspec(dllexport) void Rs7Tu8_Advance();
__declspec(dllexport) void __cdecl Rs7Tu8_OnSelect(uint8_t* session, uint32_t s);
__declspec(dllexport) void Rs7Tu8_First();
// The label of each string slot for the precision tuner (UI_NoteTuner_Initialize): the stock six
// (0x12E8644) and a label for strings 6 / 7 (their open note). Rebuilt per selection.
__declspec(dllexport) extern uintptr_t g_rs7_tu8_labels[8];

// TS (tuner.cpp Rs7_TunerApply) in native mode: the session's target. offs = the six stock offsets,
// n = 6 / 7 / 8 strings, extra = the offsets of strings 6 / 7. False = not a native session (stock).
bool Rs7Tuner8_Apply(uint8_t* session, const int* offs, int n, const int extra[2]);
// fix/menutuner: the string count of the guitar bound to `session` (+2Ch Rnd_Guitar -> +24h InstrumentInfo -> +0Bh,
// the bound every stock UI_PlayerTuner method loops to); 0 = no guitar bound yet / unreadable.
unsigned Rs7Tuner8_GuitarCount(const uint8_t* session);
// fix/menutuner: a registered grown session that is still stock while its guitar is built with 7/8 strings becomes a
// native session with its own six offsets (+50h) as a 6-string target (Rs7Tuner8_Apply n = 6). The stock methods would
// index the six-slot arrays with string 6 (CONFIRMED crash tests/owner-crash1: UI_PlayerTuner_SetupPicks 0x5B1E50 at
// 0x5B1EF5, +144h[6] = the fret byte +15Ch -> AV reading 0x1). Called by the copy dispatch (Rs7Tuner8_SessTarget)
// before every session method that has a native copy. True = the session is native now (or was).
bool Rs7Tuner8_EnsureNative(uint8_t* session, const char* where);

// Mid-hook callbacks (Rs7Mid::Context).
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F1(Rs7Mid::Context& c);   // 5AFB19 mode 0 entry: curString = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F2(Rs7Mid::Context& c);   // 5AFD15 capo step: curString = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F3(Rs7Mid::Context& c);   // 5B02A7 mode 3 entry: EDI = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8RS(Rs7Mid::Context& c);   // 5AE76D session reset: tail reset
__declspec(dllexport) void __cdecl Rs7_Mid_TS8DT(Rs7Mid::Context& c);   // 5AE360 session dtor: tail release
__declspec(dllexport) void __cdecl Rs7_Mid_TS8WR(Rs7Mid::Context& c);   // 5B2D88 wrong-instrument target
__declspec(dllexport) void __cdecl Rs7_Mid_TS8SN1(Rs7Mid::Context& c);  // 5B2E10 tuned sound entry
__declspec(dllexport) void __cdecl Rs7_Mid_TS8SN2(Rs7Mid::Context& c);  // 5B2EA7 tuned sound: its note
// 5B63E0 / 5B66AD: Play_Tuning_ApplyToDetection runs (the profile's six strings; core/pitch8.h keeps
// the song's strings 6 / 7 in the detector / matcher tails meanwhile).
__declspec(dllexport) void __cdecl Rs7_Mid_TS8PA(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TS8PZ(Rs7Mid::Context& c);
}
bool Rs7Tuner8_BuildCopies();
// tuner.cpp UpdateAny: the wrong-instrument thresholds of the native sessions (up to max entries).
struct Rs7WrongEnt;
int Rs7Tuner8_WrongEntries(Rs7WrongEnt* out, int max);

// Pure helpers (unit tests).
namespace rs7tu {
constexpr uint32_t kStockSize = 0x278, kSessionSize = 0x380;
constexpr uint32_t kMagicOff = 0x278, kMagic = 0x38545552;  // 'RUT8'
constexpr uint32_t kOpen = 0x280, kOffsets = 0x2A0, kPeaks = 0x2C8;
constexpr uint32_t kLetters = 0x2E8, kPicks = 0x328, kLastPick = 0x348;
constexpr uint32_t kCount = 0x368;   // u8 the tuner's string count N (stock 6 / 4)
constexpr uint32_t kNative = 0x369;  // u8 1 = native session (TS made it one; until the dtor)
constexpr uint32_t kOrder = 0x36C;   // u8[8] the tuning order, lowest string first
static_assert(kPeaks - kOpen == 0x48, "0x5B0AA0 addresses the targets relative to the peaks");
// Stock session arrays.
constexpr uint32_t kStockOpen = 0x38, kStockOff = 0x50, kStockPeaks = 0x80;
constexpr uint32_t kStockLetters = 0x114, kStockPicks = 0x144, kStockLast = 0x160, kCur = 0x74;
// Base MIDI per string: the stock guitar table 0x1199C8C, then B1 / F#1 (as pitch8's g_rs7_pitch_gbase).
constexpr int kGuitarBase[8] = {40, 45, 50, 55, 59, 64, 35, 30};
// The tuning order of n strings: the strings above 6 lowest first, then 0..5 (n <= 6: 0..n-1).
void Order(int n, uint8_t out[8]);
// One step from cur along order[0..n): the next / previous string, or -1.
int Step(const uint8_t* order, int n, int cur, int dir);

// Whether TS makes / keeps a session native: a 7/8-string target, an already native session, or (fix/menutuner) a
// guitar built with more than six strings whatever the target (never the stock six-slot arrays on a 7-built guitar).
constexpr bool GoesNative(int n, bool wasNative, unsigned guitarCount) {
    return n > 6 || wasNative || guitarCount > 6;
}

// Open MIDI of every slot from the six offsets and the extra two (guitar).
void Targets(const int* offs, const int extra[2], int open[8], int off[8]);
// The label text of an extra string from its open MIDI ("B", "F#", ...).
const char* NoteName(int midi);
}  // namespace rs7tu
