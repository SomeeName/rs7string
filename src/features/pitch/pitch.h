#pragma once
#include "model/instrument.h"
#include "model/tuning.h"
#include <cstddef>
#include <cstdint>

struct RsxrHookTable;
// PA1 PA2 PM0 PR0 PT1 PM1 PE0 PC1-PC4 (group Pitch, written only with the native pitch storage) and S1 S3 I1 S4
// (the song's 7-string mode at detection setup, group Core): registration.cpp.
extern const RsxrHookTable kRsxrPitchHooks;
extern const RsxrHookTable kRsxrPitchCoreHooks;

// ---- the song's 7-string mode (hooks_mode.cpp; stub S1 in hooks_stubs_mode.cpp, mid-hooks S3 / I1 in hooks_mid.cpp,
// S4 in hooks_calls.cpp) ---- Game layouts, September 2022 exe (docs/ARCHITECTURE.md, Q1-Q3).
namespace rsxr {
constexpr uint32_t kDetInstrument = 0x11EC;  // detector: instrument type, 2 = bass
constexpr uint32_t kDetFloor = 0x11F4;       // detector: raw candidate floor MIDI (reset 0x4DBF73 writes 24)
constexpr uint32_t kDetLowestOpen = 0x11F8;  // detector: lowest open MIDI (pitch filter)
constexpr uint32_t kDetOwner = 0x1238;       // detector: Play_DetectInput*
constexpr uint32_t kDetTuning = 0x1340;      // detector: int16 tuning[6]
constexpr uint32_t kInputMatcher = 0x8;      // Play_DetectInput: matcher*
constexpr uint32_t kMatcherDetector = 0x4;   // matcher: detector*
constexpr uint32_t kMatcherTuning = 0x44;    // matcher: int16 tuning[6]
}  // namespace rsxr

// Called from the asm stubs (cdecl, integer-only - no x87 code: callers keep values in FPU registers).
// S1: base MIDI of string 6 for this tuning array (35 + t6), or rsxr::kNotActive.
extern "C" __declspec(dllexport) int __cdecl Rsxr_Base6(const void* tuningKey);
// S4: song setup handed the SNG tuning to the detector. Sets or clears 7-string mode.
extern "C" __declspec(dllexport) void __cdecl Rsxr_OnSongTuning(void* session, void* detector,
                                                                const RsxrInt16Vec* tuning);
// S3: Play_Detector_SetTuning just copied `tuning` (ESI, the caller's std::vector<int16>) into the
// detector. A 7-entry vector on a non-bass detector lowers its lowest open MIDI to 35 + tuning[6]
// (never raises it); anything else stays stock. Reads no side-store state.
extern "C" __declspec(dllexport) void __cdecl Rsxr_OnDetectorTuned(void* detector, const RsxrInt16Vec* tuning);
// PT1 / PR0 / S4: extended bass strings 4 / 5 on the detector the song bound (features/bass/bass.h): open MIDI,
// lowest open MIDI, raw floor 22; stock floor back on a bass detector without the mode. Integer only, never throws.
extern "C" __declspec(dllexport) void __cdecl RsxrPitch_BassDetector(uint8_t* det);
// Per song (song setup): the low B / high C bend holds of the last song (a new note may reuse an address).
void RsxrPitch_ResetBends();

// ---- its state per detector / matcher (sidestore.cpp) ----
// 7-string state per game object, keyed by the address of the object's 6-entry tuning array
// (detector + 0x1340, matcher + 0x44). Thread-safe.
struct RsxrEntry {
    bool active;  // 7-string mode for this object
    int16_t t6;   // tuning offset of string 6 relative to B1
};

void RsxrStore_Set(const void* key, RsxrEntry e);
bool RsxrStore_Get(const void* key, RsxrEntry* out);  // false if the key was never set (or set inactive)
bool RsxrStore_AnyActive();                           // some key is in 7-string mode
// Empties the store. Also constructs the map on first use: Rsxr_Startup calls it before patching
// so the construction (which allocates) never happens inside a game frame.
void RsxrStore_ClearAll();

// Leak audit: active entries kept at most (older ones are dead sessions); the number of entries now.
constexpr size_t kRsxrStoreMax = 64;
int RsxrStore_Count();

// ---- the native pitch storage (hooks.cpp, logic pitch.cpp, mid-hooks hooks_mid.cpp and hooks_calls.cpp) ----

// Native 8-entry tuning / open-MIDI storage of the detector and matcher (docs/ARCHITECTURE.md), a member of the
// native 7-string unit (no ini switch since 1.0; needs MSVCR100's _msize).
//
// Both objects keep every stock field; the stock code still writes and reads the six-entry arrays.
// The grown allocations (PA1/PA2) carry capacity-8 tails the DLL maintains next to the stock arrays
// (PR0 reset, S3/PT1 SetTuning exits, PM0 matcher ctor, PM1 matcher SetTuning), so slots 0..5 of a
// tail always equal the stock array. The expected-pitch ports (PE0 for 0x873E90, rsxrp::ExpectedPitch8 for 0x873F80
// via the PC1-PC4 call sites) read strings 6/7 from them. The active count stays 6 / 4 (bass): this step publishes no
// new count.
//
// Detector (stock 13B8h -> 13E0h): int16 tuning8 +13B8h, int16 openMidi8 +13C8h, dword active count
// +13D8h, dword magic +13DCh. Matcher (stock 58h -> 6Ch): int16 tuning8 +58h, dword magic +68h.
// Tails are written only in blocks the CRT reports large enough (_msize), and read only in objects the DLL wrote them
// in (the registry of tailed allocations, hooks.cpp), so an object allocated before PA1/PA2 were written is never
// overrun or read past; the magic marks a valid tail.
namespace rsxrp {
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
constexpr uint32_t kMagic = 0x38505852;  // 'RXP8'
constexpr uint32_t kDetOpen6 = 0x134C;   // stock int16 openMidi[6]
// Entries of the registry of tailed allocations (hooks.cpp). CONFIRMED in game (2026-10-18, temporary counter, smoke
// song7, song6, pause tuner, menu tuner: 34 tail writes): peak 2 registered objects (one matcher, one detector, the
// same two all session), 0 evictions. 128 = the floor of the margin (at least 4 x the peak, at least 128).
constexpr unsigned kTailedMax = 128;
}  // namespace rsxrp

extern "C" {
__declspec(dllexport) extern uintptr_t g_rsxr_native_pitch;  // set before patching
// 1 while Play_Tuning_ApplyToDetection 0x5B63E0 runs (sites TS8PA / TS8PZ, native tuner only):
// it re-applies the PROFILE's six strings (e.g. around a tuner visit). A song's strings 6 / 7 (the last
// SetTuning with 7 / 8 entries on the same detector / matcher) then stay in the tails and in the
// lowest open MIDI instead of being zeroed. 0 (always without the native tuner): stock.
__declspec(dllexport) extern uintptr_t g_rsxr_profile_apply;
__declspec(dllexport) extern uintptr_t g_rsxr_fn_msize;  // MSVCR100 _msize (the game's operator new heap)
// Base MIDI per string (the stock case constants of 0x873E90 / 0x873F80, plus B1 / F#1 for 6 / 7).
__declspec(dllexport) extern const int32_t g_rsxr_pitch_gbase[8];
__declspec(dllexport) extern const int32_t g_rsxr_pitch_bbase[6];

// Called from the stubs (cdecl, integer only - callers keep values in x87 registers; none throws).
__declspec(dllexport) void __cdecl RsxrPitch_DetReset(uint8_t* det);  // PR0
// The string count a 7/8-string song widened the detector to at S4 (features/publish/publish.h); 0 = stock. Set 0
// clears. PR0 (a new / reset detector) clears it.
__declspec(dllexport) void __cdecl RsxrPitch_SetSongCount(const uint8_t* det, unsigned count);
__declspec(dllexport) unsigned __cdecl RsxrPitch_SongCount(const uint8_t* det);
__declspec(dllexport) void __cdecl RsxrPitch_DetTuned(uint8_t* det, const RsxrInt16Vec* v);  // S3 / PT1
// The raw floor follows the lowest open MIDI (rsxrp::Floor) after every writer of it: PR0 (reset = 1: the floor the
// reset just wrote is this detector's own), PT1, S3, S4 and the tuner's low range. Cold, game thread, never throws.
__declspec(dllexport) void __cdecl RsxrPitch_DetFloor(uint8_t* det, int reset);
// DetFloor's log line of a changed floor (its own export: the emulator tests replace loggers with `ret`).
__declspec(dllexport) void __cdecl RsxrPitch_FloorLog(uint8_t* det, int floor, int want, int lowest, int reset);
__declspec(dllexport) void __cdecl RsxrPitch_MatNew(uint8_t* m);                           // PM0
__declspec(dllexport) void __cdecl RsxrPitch_MatTuned(uint8_t* m, const RsxrInt16Vec* v);  // PM1
// Whether a profile re-apply keeps the song's extra strings (features/tuner/hooks_session.cpp: native tuner on).
int RsxrPitch_KeepSongTuning();
// PC1-PC4, the publication (strings >= 6 only): 1 = `obj` has a valid tail - the DLL wrote it (the registry of tailed
// allocations, filled only after _msize saw the native block size, so nothing past a stock-size block is read) and
// it carries the magic. kind 0 = matcher, 1 = detector. Light-safe: no CRT call, no x87/SSE instruction.
__declspec(dllexport) int __cdecl RsxrPitch_Tailed(uint8_t* obj, uint32_t kind);
// Empties the registry of tailed allocations (tests).
void RsxrPitch_ClearTailed();
}

// Pure parts (unit tests).
namespace rsxrp {
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

// The detector's raw floor (docs/ARCHITECTURE.md, "Low tunings"): every MIDI from it up is evaluated per frame
// (0x4DF2B0), and only peaks at or above it count (0x4DF613, 0x4E01B7). `reset` is what Play_Detector_Reset wrote (24,
// or a floor another mod patched in); a song whose lowest open string is below it gets one semitone below that string
// (B0 = 23 -> 22, A0 = 21 -> 20), never less than kMinFloor. Otherwise `reset` unchanged.
constexpr int kStockFloor = 24;  // Play_Detector_Reset 0x4DBF73
// 0x4DF4D0 reads a note's strength at lag = rate / frequency from a buffer of 2048 (+DD4h), clamped to 2044; at 48 kHz
// (+DCCh; both CONFIRMED in game) that is 23.5 Hz = MIDI 18.3, so G0 (19, lag 1959) is the lowest open string whose
// pitch fits. At 44.1 kHz the limit is lower.
constexpr int kMinFloor = 18;
int Floor(int reset, int lowest);
}  // namespace rsxrp
