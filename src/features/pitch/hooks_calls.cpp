// Call-site mid-hooks of the pitch feature (Before placement, ARCHITECTURE.md section 15 "Call sites": the callback
// runs at the callee's entry, [esp] = the game's return address, the arguments above it). S4 runs once per song setup
// (full), PC2 once per tracked note (Play_Eval_Build, from Play_Tracker_AddNoteOrChord / AddChordStringNote) and PC3
// once per analysed mistake (full). PC1 (Play_Matcher_IsStringFretPlaying) and PC4 (the slide-target eval of
// Play_Tracker_Update) run every frame per tracked note: light mode, integer only (Rs7Pitch_Tailed reads the registry
// of tailed allocations, no _msize). Converted from naked stubs (PC2 / PC3 frozen at b6071a8,
// tools/tests/test_midhook_calls.py; PC1 / PC4 frozen at 72da588, tools/tests/test_midhook_asm_sites2.py); the old
// stubs' flags are dead at every exit.
#include "features/pitch/pitch.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;

namespace {
// A call of 0x873F80 (ECX = int16 tuning*, EDX = instrument, ESI = string, [esp+4] = fret, plain ret) whose tuning
// pointer lies in a matcher or detector that may carry the native tuning8 tail.
struct TailedCall {
    uint32_t tuning;  // the stock tuning pointer's offset in its object (ECX - tuning = the matcher / detector)
    uint32_t kind;    // Rs7Pitch_Tailed's kind: 0 = matcher, 1 = detector
    uint32_t tail;    // the tuning8 tail's offset in the object
};

constexpr TailedCall kMatcherCall = {rs7::kMatcherTuning, 0, rs7p::kMatTuning8};
constexpr TailedCall kDetectorCall = {rs7::kDetTuning, 1, rs7p::kDetTuning8};

// Strings 0..5, a null pointer or an object without the tail: the stock callee (S1 included) runs unchanged. Strings
// >= 6 of a native object: the callee is replaced by the expected pitch over the tail, with the stock register effects
// (EAX = the pitch, EDX = instrument - 2, ECX / ESI kept; the caller pops the fret).
void TailedPitch(Context& c, const TailedCall& s) {
    if (c.esi < 6 || !c.ecx) {
        return;
    }

    uint8_t* object = reinterpret_cast<uint8_t*>(c.ecx - s.tuning);

    if (!Rs7Pitch_Tailed(object, s.kind)) {
        return;
    }

    const auto* tuning8 = reinterpret_cast<const int16_t*>(object + s.tail);
    const uint32_t instrument = c.edx;

    c.edx = instrument - 2;
    Rs7Mid::Return(c, rs7p::ExpectedPitch8(tuning8, instrument, c.esi, Rs7Mid::Dword(c.esp + 4)));
}
}  // namespace

extern "C" {
// The callees (bound by Rs7_Startup; the entries call them directly).
__declspec(dllexport) uintptr_t g_rs7_target_PC2 = 0, g_rs7_target_PC3 = 0, g_rs7_target_S4 = 0;

__declspec(dllexport) uintptr_t g_rs7_target_PC1 = 0, g_rs7_target_PC4 = 0;

// PC1 - 0x4E54FB in Play_Matcher_IsStringFretPlaying: the tuning pointer is matcher+44h (or null). Light.
__declspec(dllexport) void __cdecl Rs7_Mid_PC1(Context& c) {
    TailedPitch(c, kMatcherCall);
}

// PC4 - 0x8768A6, the slide-target eval of Play_Tracker_Update: the tuning pointer is matcher+44h (or null). Light.
__declspec(dllexport) void __cdecl Rs7_Mid_PC4(Context& c) {
    TailedPitch(c, kMatcherCall);
}

// PC2 - 0x8742CC in Play_Eval_Build: the tuning pointer is matcher+44h (or null).
__declspec(dllexport) void __cdecl Rs7_Mid_PC2(Context& c) {
    TailedPitch(c, kMatcherCall);
}

// PC3 - 0x8754AA in Play_AnalyzeNoteMistake: the tuning pointer is detector+1340h.
__declspec(dllexport) void __cdecl Rs7_Mid_PC3(Context& c) {
    TailedPitch(c, kDetectorCall);
}

// S4 - 0x7DF0FC, the song-setup call to Play_Detector_SetTuning 0x4E2590 (EAX = detector, ESI = SNG tuning vector,
// EBX = session): Rs7_OnSongTuning sets or clears the 7-string mode first, then the callee runs.
__declspec(dllexport) void __cdecl Rs7_Mid_S4(Context& c) {
    Rs7_OnSongTuning(reinterpret_cast<void*>(c.ebx), reinterpret_cast<void*>(c.eax),
                     reinterpret_cast<const Rs7Int16Vec*>(c.esi));
}
}
