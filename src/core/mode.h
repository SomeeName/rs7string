#pragma once
#include <cstdint>

// Game layouts, September 2022 exe (docs/ARCHITECTURE.md, Q1-Q3).
namespace rs7 {
constexpr int kLowB = 35;                    // B1: base MIDI of string index 6
constexpr int kNotActive = INT32_MIN;        // Rs7_Base6: not in 7-string mode
constexpr uint32_t kDetInstrument = 0x11EC;  // detector: instrument type, 2 = bass
constexpr uint32_t kDetLowestOpen = 0x11F8;  // detector: lowest open MIDI (pitch filter)
constexpr uint32_t kDetOwner = 0x1238;       // detector: Play_DetectInput*
constexpr uint32_t kDetTuning = 0x1340;      // detector: int16 tuning[6]
constexpr uint32_t kInputMatcher = 0x8;      // Play_DetectInput: matcher*
constexpr uint32_t kMatcherDetector = 0x4;   // matcher: detector*
constexpr uint32_t kMatcherTuning = 0x44;    // matcher: int16 tuning[6]
constexpr uint32_t kSessionGuitar = 0x37C;   // play session: Rnd_Guitar* (Play_Session_SetupDetection 0x7DEFB0)
}  // namespace rs7

// The game's std::vector<int16> (MSVC 2010): begin at +0, end at +4.
struct Rs7Int16Vec {
    const int16_t* begin;
    const int16_t* end;
};

// Called from the asm stubs (cdecl, integer-only - no x87 code: callers keep values in FPU registers).
// S1: base MIDI of string 6 for this tuning array (35 + t6), or rs7::kNotActive.
extern "C" int __cdecl Rs7_Base6(const void* tuningKey);
// S4: song setup handed the SNG tuning to the detector. Sets or clears 7-string mode.
extern "C" void __cdecl Rs7_OnSongTuning(void* session, void* detector, const Rs7Int16Vec* tuning);
// S3: Play_Detector_SetTuning just copied `tuning` (ESI, the caller's std::vector<int16>) into the
// detector. A 7-entry vector on a non-bass detector lowers its lowest open MIDI to 35 + tuning[6]
// (never raises it); anything else stays stock. Reads no side-store state.
extern "C" void __cdecl Rs7_OnDetectorTuned(void* detector, const Rs7Int16Vec* tuning);
// S2 (diagnostics): log the expected MIDI of a string-6 fret once per song.
extern "C" void __cdecl Rs7_NoteString6(int fret, int t6);
