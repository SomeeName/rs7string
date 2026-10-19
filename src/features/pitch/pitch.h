#pragma once
#include "model/tuning.h"
#include <cstddef>
#include <cstdint>

struct Rs7HookTable;
constexpr int kRs7PitchNative = 1;  // Rs7Hook::pitch: written only with the native 8-entry tuning / open-MIDI storage
// PA1 PA2 PM0 PR0 PT1 PM1 PE0 PC1-PC4 (group Pitch, written only with the native pitch storage) and S1 S2 S3 I1 S4
// (the song's 7-string mode at detection setup, group Core): registration.cpp.
extern const Rs7HookTable kRs7PitchHooks;
extern const Rs7HookTable kRs7PitchCoreHooks;

// ---- the song's 7-string mode (hooks_mode.cpp, stubs S1-S4 / I1 in hooks_stubs_mode.cpp) ----
// Game layouts, September 2022 exe (docs/ARCHITECTURE.md, Q1-Q3).
namespace rs7 {
constexpr uint32_t kDetInstrument = 0x11EC;  // detector: instrument type, 2 = bass
constexpr uint32_t kDetLowestOpen = 0x11F8;  // detector: lowest open MIDI (pitch filter)
constexpr uint32_t kDetOwner = 0x1238;       // detector: Play_DetectInput*
constexpr uint32_t kDetTuning = 0x1340;      // detector: int16 tuning[6]
constexpr uint32_t kInputMatcher = 0x8;      // Play_DetectInput: matcher*
constexpr uint32_t kMatcherDetector = 0x4;   // matcher: detector*
constexpr uint32_t kMatcherTuning = 0x44;    // matcher: int16 tuning[6]
constexpr uint32_t kSessionGuitar = 0x37C;   // play session: Rnd_Guitar* (Play_Session_SetupDetection 0x7DEFB0)
}  // namespace rs7

// Called from the asm stubs (cdecl, integer-only - no x87 code: callers keep values in FPU registers).
// S1: base MIDI of string 6 for this tuning array (35 + t6), or rs7::kNotActive.
extern "C" __declspec(dllexport) int __cdecl Rs7_Base6(const void* tuningKey);
// S4: song setup handed the SNG tuning to the detector. Sets or clears 7-string mode.
extern "C" __declspec(dllexport) void __cdecl Rs7_OnSongTuning(void* session, void* detector,
                                                               const Rs7Int16Vec* tuning);
// S3: Play_Detector_SetTuning just copied `tuning` (ESI, the caller's std::vector<int16>) into the
// detector. A 7-entry vector on a non-bass detector lowers its lowest open MIDI to 35 + tuning[6]
// (never raises it); anything else stays stock. Reads no side-store state.
extern "C" __declspec(dllexport) void __cdecl Rs7_OnDetectorTuned(void* detector, const Rs7Int16Vec* tuning);
// S2 (diagnostics): log the expected MIDI of a string-6 fret once per song.
extern "C" void __cdecl Rs7_NoteString6(int fret, int t6);

// ---- its state per detector / matcher (sidestore.cpp) ----
// 7-string state per game object, keyed by the address of the object's 6-entry tuning array
// (detector + 0x1340, matcher + 0x44). Thread-safe.
struct Rs7Entry {
    bool active;  // 7-string mode for this object
    int16_t t6;   // tuning offset of string 6 relative to B1
};

void Rs7Store_Set(const void* key, Rs7Entry e);
bool Rs7Store_Get(const void* key, Rs7Entry* out);  // false if the key was never set (or set inactive)
bool Rs7Store_AnyActive();                          // some key is in 7-string mode
// Empties the store. Also constructs the map on first use: Rs7_Startup calls it before patching
// so the construction (which allocates) never happens inside a game frame.
void Rs7Store_ClearAll();

// Leak audit: active entries kept at most (older ones are dead sessions); the number of entries now.
constexpr size_t kRs7StoreMax = 64;
int Rs7Store_Count();

// ---- the native pitch storage (hooks.cpp, logic pitch.cpp, stubs hooks_stubs.cpp) ----

// Native 8-entry tuning / open-MIDI storage of the detector and matcher (issue #18 stage 5 area 3,
// docs/ARCHITECTURE.md), a member of the native 7-string unit (no ini switch since 1.0; needs MSVCR100's _msize).
//
// Both objects keep every stock field; the stock code still writes and reads the six-entry arrays.
// The grown allocations (PA1/PA2) carry capacity-8 tails the DLL maintains next to the stock arrays
// (PR0 reset, S3/PT1 SetTuning exits, PM0 matcher ctor, PM1 matcher SetTuning), so slots 0..5 of a
// tail always equal the stock array. The expected-pitch ports (PE0 for 0x873E90, rs7p::ExpectedPitch8 for 0x873F80
// via the PC1-PC4 call sites) read strings 6/7 from them. The active count stays 6 / 4 (bass): this step publishes no
// new count.
//
// Detector (stock 13B8h -> 13E0h): int16 tuning8 +13B8h, int16 openMidi8 +13C8h, dword active count
// +13D8h, dword magic +13DCh. Matcher (stock 58h -> 6Ch): int16 tuning8 +58h, dword magic +68h.
// Tails are written only in blocks the CRT reports large enough (_msize), and read only in objects the DLL wrote them
// in (the registry of tailed allocations, hooks.cpp), so an object allocated before PA1/PA2 were written is never
// overrun or read past; the magic marks a valid tail.
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
// Entries of the registry of tailed allocations (hooks.cpp). CONFIRMED in game (2026-10-18, temporary counter, smoke
// song7, song6, pause tuner, menu tuner: 34 tail writes): peak 2 registered objects (one matcher, one detector, the
// same two all session), 0 evictions. 128 = the floor of the margin (at least 4 x the peak, at least 128).
constexpr unsigned kTailedMax = 128;
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
// The string count a 7/8-string song widened the detector to at S4 (features/publish/publish.h); 0 = stock. Set 0
// clears. PR0 (a new / reset detector) clears it.
__declspec(dllexport) void __cdecl Rs7Pitch_SetSongCount(const uint8_t* det, unsigned count);
__declspec(dllexport) unsigned __cdecl Rs7Pitch_SongCount(const uint8_t* det);
__declspec(dllexport) void __cdecl Rs7Pitch_DetTuned(uint8_t* det, const Rs7Int16Vec* v);  // S3 / PT1
__declspec(dllexport) void __cdecl Rs7Pitch_MatNew(uint8_t* m);                            // PM0
__declspec(dllexport) void __cdecl Rs7Pitch_MatTuned(uint8_t* m, const Rs7Int16Vec* v);    // PM1
// Whether a profile re-apply keeps the song's extra strings (features/tuner/hooks_session.cpp: native_tuner on).
int Rs7Pitch_KeepSongTuning();
// PC1-PC4, the publication (strings >= 6 only): 1 = `obj` has a valid tail - the DLL wrote it (the registry of tailed
// allocations, filled only after _msize saw the native block size, so nothing past a stock-size block is read) and
// it carries the magic. kind 0 = matcher, 1 = detector. Light-safe: no CRT call, no x87/SSE instruction.
__declspec(dllexport) int __cdecl Rs7Pitch_Tailed(uint8_t* obj, uint32_t kind);
// Empties the registry of tailed allocations (tests).
void Rs7Pitch_ClearTailed();
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
// The expected pitch 0x873F80 computes, over an eight-entry tuning (the stock arithmetic, 32-bit wrap):
// instrument 2 (bass) strings 0..3 and every other instrument's strings 0..7 tuning8[s] + base[s] + fret, any other
// string fret - 1.
uint32_t ExpectedPitch8(const int16_t tuning8[8], uint32_t instrument, uint32_t string, uint32_t fret);
}  // namespace rs7p
