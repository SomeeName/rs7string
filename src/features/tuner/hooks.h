#pragma once
#include "platform/midhook.h"
#include <cstdint>

// The tuner feature's game glue (internal: other code includes features/tuner/tuner.h). Exported names are what the
// stubs and the emulator tests (tools/tests) call.
//
// Shared tuner hooks (hooks.cpp; docs/ARCHITECTURE.md, "7th slot in game"; GitHub issues #1 and #8):
//  - TK (Play_Tuning_ReadStringOffsets 0x5B8480 entry; its callers are the tuner menu UI_TunerMenu_Enter, the
//    MP/pre-song tuner menu 0x742D50 (both players) and 0x5B76C0): reads the manifest keys (Rs7String6, never
//    "string6": the song-list reader 0x56D0B0 overflows on it; Rs7String7 for the 8th string) from the target tuning
//    JSON (the game's own JSON vtable, as 0x5B84DC) and keeps them for that offset array (menu+1B8h / +1D0h). The
//    tuning names (TN) use them too (Rs7Tuner_SevenFor).
//  - TS (Play_TunerSession_ApplyTuning 0x5AF200 entry, session + that offset array): hands the session's target
//    (6, 7 or 8 strings) to the native session (Rs7Tuner8_Apply); without the native tuner, stock.
//  - W (UI_PlayerTuner_CheckWrongInstrument): the low-range threshold below.
//  - AT (UI_PlayerTuner_IsStringTuned 0x5B0C20 entry): [debug] tuner_autotune=N only (test driving without a guitar):
//    "tuned" N seconds after the session's first query. Default off.
// Every stub replays stock after one global read (g_rs7_tuner_any) while no native session or autotune exists.
//
// Native 8-slot tuner session (hooks_session.cpp, hooks_methods.cpp, hooks_copies.cpp; issue #18 job 3). Needs the
// native strings, FretGrid and colours:
//  - Every UI_PlayerTuner is allocated with 380h bytes (TU8ALLOC) and registered (TU8INIT / TU8FREE); the stock 278h
//    prefix is unchanged.
//  - A session whose target tuning has 7 or 8 strings (TK; or a published InstrumentInfo +0Bh of 7/8 with the
//    detector's tuning8 tail, feat/publish) becomes a NATIVE session at TS: its six per-string arrays move to the
//    capacity-8 tail below (values copied, entity pointers moved), its string count N and its tuning order (lowest
//    string first: 7 strings = 6,0,1,2,3,4,5; 8 = 7,6,0,...,5) are stored there.
//  - The UI_PlayerTuner methods (TS8* sites, tools/gen_tuner8s.py) run as relocated copies for a native session only:
//    arrays from the tail, loops over N, steps through the order (Rs7Tu8_AdvanceC). Isolated reads in large functions
//    are mid-hooks (TS8F*, TS8RS, TS8DT, TS8WR, TS8SN*). Every other session (6-string guitar, bass, no key) runs the
//    unmodified game code.
//  - The physical string index stays the game's (string 6 = the 7th string, its GuitarStrings slot, peg 7, FretGrid
//    cell 6, colour row 6): no slot is mirrored or re-targeted.
// The GuitarStrings readers TU8H / TU8C / TU8A (HighlightString, ClearHighlight, StringAction) run their native copies
// for every native GuitarStrings (features/strings/strings.h).
//
// Low range ([tuner] lowrange; low-range research README "Demo-first hook plan", step 2): the guitar wrong-instrument
// test (W, UI_PlayerTuner_CheckWrongInstrument 0x5B2DDE) warns when the mean of ten detected notes is <= 34.0
// (event 8), which trips on a correctly played A1/G1/F#1. For a 7-string tuner session the threshold becomes
// min(34, lowest open MIDI - 1); 6-string guitar, bass and sessions without Rs7String6 keep 34.0. The detector's
// lowest-open (+11F8h) is lowered to that lowest open note on each selection (the tuner reaches SetTuning with six
// entries, so S3 does not). The detector floor (MIDI 24, 0x4DBF73) stays: guitar needs no change, only a 5-string
// bass B0 does. TODO(bass B0): floor 22 + lowest 23 + corrected bass expected-pitch/base (docs/ARCHITECTURE.md).

extern "C" {
// ---- shared tuner hooks (hooks.cpp)

// Game data (ref, never written).
__declspec(dllexport) extern uintptr_t g_rs7_tuner_mgr;  // X_TMGR  &0x135F5D4 (tuner sessions at +10h + slot*4)
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_getdet;  // X_GETDET Play_GetDetector 0x4E8B50 (ECX input id -> detector or 0)

// The threshold per live 7-string tuner session (rebuilt by every state change, so W reads plain data).
struct Rs7WrongEnt {
    uint8_t* session;
    int threshold;
};

__declspec(dllexport) extern Rs7WrongEnt g_rs7_wrong_tab[4];
__declspec(dllexport) extern float g_rs7_wrong_thr;        // W: the session's threshold (written by Rs7_WrongThreshold)
__declspec(dllexport) extern uintptr_t g_rs7_wrong_const;  // W: &the game's threshold (ref site X_WTHR)

// What the TJ / TU / TN callbacks call (cdecl).
// W: ESI = session, x87 live (ST0 = the mean): integer/SSE only, writes g_rs7_wrong_thr.
__declspec(dllexport) void __cdecl Rs7_WrongThreshold(uint8_t* session);
__declspec(dllexport) void __cdecl Rs7_TunerKey(int* offs, void* holder);              // TK
__declspec(dllexport) void __cdecl Rs7_TunerApply(uint8_t* session, const int* offs);  // TS
__declspec(dllexport) uint32_t __cdecl Rs7_TunerAutotune(uint8_t* session);            // AT: 1 = "tuned"
// Low range for a native tuner session: the detector's lowest open lowered to lowestOpen.
void Rs7Tuner_LowerDetectorTo(const uint8_t* session, int lowestOpen);

// Mid-hook callbacks of the shared hooks and the tuning names (formerly hooks/mid_cold.cpp).
__declspec(dllexport) void __cdecl Rs7_Mid_TK(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_W(Rs7Mid::Context&);  // W, full mode (x87 compare)
__declspec(dllexport) void __cdecl Rs7_Mid_TS(Rs7Mid::Context&);
__declspec(dllexport) void __cdecl Rs7_Mid_TUT(Rs7Mid::Context&);

// ---- native session: GuitarStrings reader copies and session copies (hooks_copies.cpp)

__declspec(dllexport) int __cdecl Rs7Tuner8_Use(uint32_t kind, uint8_t* reg);
__declspec(dllexport) uintptr_t __cdecl Rs7Tuner8_Target(uint32_t kind, uint8_t* reg);
__declspec(dllexport) void Rs7_Stub_TU8H();
__declspec(dllexport) void Rs7_Stub_TU8C();
__declspec(dllexport) void Rs7_Stub_TU8A();
// The entry dispatch of a copied method: spec index, the PUSHAD frame of the stub (regs[0] = EDI ... regs[7] = EAX,
// regs[8] = the stub's slot, regs[9] = the caller's return address, regs[10] = arg 1).
__declspec(dllexport) uintptr_t __cdecl Rs7Tuner8_SessTarget(uint32_t index, const uint32_t* regs);

// ---- native session: lifetime and target (hooks_session.cpp)

__declspec(dllexport) void __cdecl Rs7Tuner8_InitSession(uint8_t* session);
__declspec(dllexport) int __cdecl Rs7Tuner8_HasSession(const uint8_t* session);
__declspec(dllexport) uint32_t __cdecl Rs7Tuner8_FreeSessionSize(uint8_t* session);
__declspec(dllexport) void Rs7_Stub_TU8INIT();
__declspec(dllexport) void Rs7_Stub_TU8FREE();
__declspec(dllexport) extern uintptr_t g_rs7_resume_TU8FREE;
// 1 = `session` is a registered grown session in native mode (its tail holds the arrays).
__declspec(dllexport) int __cdecl Rs7Tuner8_IsNative(const uint8_t* session);

// ---- native session: what the copies call, and the mid-hooks (hooks_methods.cpp, hooks_callin.cpp)

// Rs7Tu8_AdvanceC: (session, dir +1 / -1) -> 1 = curString moved one step along the tuning order, 0 = no further
// string; Rs7Tu8_FirstC: (session) -> the first string of the order. The copies call them through the call-in entries
// g_rs7_callin_Tu8Advance / _Tu8First (hooks_callin.cpp: stdcall, ECX / EDX preserved). Rs7Tu8_OnSelect: cdecl
// (session, string), inside PUSHAD.
__declspec(dllexport) void __cdecl Rs7Tu8_OnSelect(uint8_t* session, uint32_t s);
int __stdcall Rs7Tu8_AdvanceC(uint8_t* session, int dir);
uint32_t __stdcall Rs7Tu8_FirstC(uint8_t* session);
// The label of each string slot for the precision tuner (UI_NoteTuner_Initialize): the stock six (0x12E8644) and a
// label for strings 6 / 7 (their open note). Rebuilt per selection.
__declspec(dllexport) extern uintptr_t g_rs7_tu8_labels[8];

// Mid-hook callbacks (Rs7Mid::Context).
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F1(Rs7Mid::Context& c);   // 5AFB19 mode 0 entry: curString = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F2(Rs7Mid::Context& c);   // 5AFD15 capo step: curString = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8F3(Rs7Mid::Context& c);   // 5B02A7 mode 3 entry: EDI = first
__declspec(dllexport) void __cdecl Rs7_Mid_TS8RS(Rs7Mid::Context& c);   // 5AE76D session reset: tail reset
__declspec(dllexport) void __cdecl Rs7_Mid_TS8DT(Rs7Mid::Context& c);   // 5AE360 session dtor: tail release
__declspec(dllexport) void __cdecl Rs7_Mid_TS8WR(Rs7Mid::Context& c);   // 5B2D88 wrong-instrument target
__declspec(dllexport) void __cdecl Rs7_Mid_TS8SN1(Rs7Mid::Context& c);  // 5B2E10 tuned sound entry
__declspec(dllexport) void __cdecl Rs7_Mid_TS8SN2(Rs7Mid::Context& c);  // 5B2EA7 tuned sound: its note
// 5B63E0 / 5B66AD: Play_Tuning_ApplyToDetection runs (the profile's six strings; features/pitch/pitch.h keeps the
// song's strings 6 / 7 in the detector / matcher tails meanwhile).
__declspec(dllexport) void __cdecl Rs7_Mid_TS8PA(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TS8PZ(Rs7Mid::Context& c);

// ---- tuning names (hooks_names.cpp)

// Set by Rs7_Startup before any site is patched: 0 = TJ/TU/TUT/TN replay stock without a C call.
__declspec(dllexport) extern uintptr_t g_rs7_tn_on;
// Game functions (callref sites, never written).
__declspec(dllexport) extern uintptr_t g_rs7_fn_strcon;   // X_STRCON  0x4079D0
__declspec(dllexport) extern uintptr_t g_rs7_fn_strasg;   // X_STRASG  0x407840
__declspec(dllexport) extern uintptr_t g_rs7_fn_loc;      // X_LOC     0x7BF420 (context *0x135FBFC = X_DT)
__declspec(dllexport) extern uintptr_t g_rs7_fn_strdtor;  // X_STRDTOR 0x85C810

// What the TJ / TU / TN callbacks call (cdecl).
// TJ: 0 = not a 7-string Tuning (stock only), else a handle for Rs7_TnJsonPost.
__declspec(dllexport) uint32_t __cdecl Rs7_TnJsonPre(void* raw, void* disp, void* json, int cents);
__declspec(dllexport) void __cdecl Rs7_TnJsonPost(uint32_t handle);
// TU: handle (0 = off); TUT records the Tuning/Attributes for the current thread's open call.
__declspec(dllexport) uint32_t __cdecl Rs7_TnUuidPre(void* out);
__declspec(dllexport) void __cdecl Rs7_TnUuidTuning(void* tuning, void* attributes);
__declspec(dllexport) void __cdecl Rs7_TnUuidPost(uint32_t handle);
// TN: 1 = out was constructed with the 7-string name (skip the original); 0 = run it. With 0 and *post set, call
// Rs7_TnArrayPost after the original (mark an unknown tuning's stock name).
__declspec(dllexport) uint32_t __cdecl Rs7_TnArrayPre(void* out, const int* offs, uint32_t* post);
__declspec(dllexport) void __cdecl Rs7_TnArrayPost(void* out, uint32_t post);

// The Wrap mid-hooks of the three resolvers (hooks_wrap.cpp): before and after callbacks.
__declspec(dllexport) void __cdecl Rs7_Mid_TJBefore(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TJ(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TUBefore(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TU(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TNBefore(Rs7Mid::Context& c);
__declspec(dllexport) void __cdecl Rs7_Mid_TN(Rs7Mid::Context& c);
}

// The session copies' stdcall calls (hooks_callin.cpp).
extern Rs7Mid::CallIn g_rs7_callin_Tu8Advance, g_rs7_callin_Tu8First;

// The resolvers' frames: TJ (Tuning JSON, CentOffset; cdecl), TU (no stack arguments), TN (output, offsets; cdecl).
inline constexpr Rs7Mid::Wrap kRs7WrapTJ{2, Rs7Mid::Conv::Cdecl};
inline constexpr Rs7Mid::Wrap kRs7WrapTU{0, Rs7Mid::Conv::Cdecl};
inline constexpr Rs7Mid::Wrap kRs7WrapTN{2, Rs7Mid::Conv::Cdecl};

// ---- shared tuner hooks (hooks.cpp)

namespace rs7tn {
struct Config;
}

// Rebuilds the wrong-instrument table and the stubs' fast-path flag from the native sessions.
void Rs7Tuner_Refresh();
void Rs7Tuner_ResetAll();
// Tuning names (TN): whether `offs` is an offset array TK read Rs7String6 for (the tuner menus' +1B8h / +1D0h,
// 0x5B76C0's stack array); *t6 = the 7th string's offset.
bool Rs7Tuner_SevenFor(const int* offs, int* t6);

// ---- tuning names (hooks_names.cpp)

void Rs7Tn_Configure(const rs7tn::Config& c);

// ---- native session (hooks_session.cpp)

extern "C" {
// TS (Rs7_TunerApply) in native mode: the session's target. offs = the six stock offsets, n = 6 / 7 / 8 strings,
// extra = the offsets of strings 6 / 7. False = not a native session (stock).
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
}
// Rs7Tuner_Refresh: the wrong-instrument thresholds of the native sessions (up to max entries).
int Rs7Tuner8_WrongEntries(Rs7WrongEnt* out, int max);

namespace rs7tuner {
// A native session's string count N (hooks_session.cpp).
uint8_t Count(const uint8_t* session);
// The lowest open MIDI of a native session's strings (hooks_session.cpp).
int LowestOpen(const uint8_t* session);
// The precision tuner's labels of strings 6 / 7 (g_rs7_tu8_labels) from the session's open notes (hooks_session.cpp).
void SetLabels(const uint8_t* session);
// The native session's log, at most 300 lines (hooks_session.cpp).
void Log(const char* fmt, const uint8_t* session, int a = 0, int b = 0, int c = 0);

// The game's JSON objects (as 0x56C950 / 0x889C50 / 0x5B84DC read them): vtable +14h find(name) -> node or 0, node +50h
// -> number object or 0, the double at +8. False on any fault.
bool JsonNumber(void* obj, const char* key, double* out);
// A tuning offset: a number in (-25, 25), truncated as the stock readers do (0xF5B0D0).
bool JsonOffset(void* obj, const char* key, int* out, double* raw = nullptr);

using Rs7Mid::Dword;  // platform/midhook.h

template <class T>
T Read(const uint8_t* p, uint32_t off) {
    return *reinterpret_cast<const T*>(p + off);
}

template <class T>
T& At(uint8_t* p, uint32_t off) {
    return *reinterpret_cast<T*>(p + off);
}
}  // namespace rs7tuner

// UI_PlayerTuner (stock 278h bytes, docs/ARCHITECTURE.md "Session state machine") and the native tail.
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
// The session's Rnd_Guitar (+2Ch), its Rnd_InstrumentInfo (+24h) and that one's string count (+0Bh).
constexpr uint32_t kGuitar = 0x2C, kGuitarInfo = 0x24, kInfoCount = 0xB;
}  // namespace rs7tu
