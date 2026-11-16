// Call-site mid-hooks of the pitch feature (Before placement, ARCHITECTURE.md section 14 "Call sites": the callback
// runs at the callee's entry, [esp] = the game's return address, the arguments above it). S4 runs once per song setup
// (full), PC2 once per tracked note (Play_Eval_Build, from Play_Tracker_AddNoteOrChord / AddChordStringNote) and PC3
// once per analysed mistake (full). PC1 (Play_Matcher_IsStringFretPlaying) and PC4 (the slide-target eval of
// Play_Tracker_Update) run every frame per tracked note: light mode, integer only (Rs7Pitch_Tailed reads the registry
// of tailed allocations, no _msize). Converted from naked stubs (PC2 / PC3 frozen at b6071a8,
// tools/tests/test_midhook_calls.py; PC1 / PC4 frozen at 72da588, tools/tests/test_midhook_asm_sites2.py); the old
// stubs' flags are dead at every exit.
#include "features/bass/bass.h"
#include "features/pitch/pitch.h"
#include "features/pitch/state.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <cstdint>
#include <cstring>

using Rs7Mid::Context;
using rs7p::g_state;

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

// Bass strings 4 / 5 of an extended-bass song (bass.h; the stock callee rejects them with fret - 1): the stock
// six-entry tuning holds their offsets, so EAX = base (23 / 48) + tuning[string] + fret, whatever object the pointer
// lies in.
bool BassPitch(Context& c) {
    const int base = rs7bass::ExtraBase(c.esi, g_rs7_bass_strings);

    if (!rs7::IsBassKind(int(c.edx)) || !c.ecx || base == rs7bass::kNoBase) {
        return false;
    }

    const auto* tuning = reinterpret_cast<const int16_t*>(c.ecx);

    c.edx = 0;  // instrument - 2
    Rs7Mid::Return(c, uint32_t(int32_t(tuning[c.esi]) + base) + Rs7Mid::Dword(c.esp + 4));

    return true;
}

// Strings 0..5, a null pointer or an object without the tail: the stock callee (S1 included) runs unchanged. Strings
// >= 6 of a native object: the callee is replaced by the expected pitch over the tail, with the stock register effects
// (EAX = the pitch, EDX = instrument - 2, ECX / ESI kept; the caller pops the fret).
void TailedPitch(Context& c, const TailedCall& s) {
    if (BassPitch(c) || c.esi < 6 || !c.ecx) {
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

// One line per low B note that bent: how often the tracker had no pitch or read the bend far too low (held frames).
void LogBend(const rs7bass::BendHold& h) {
    if (!h.valid || h.peak < 0.5f || g_state.bendLogged >= 40) {
        return;
    }

    ++g_state.bendLogged;

    try {  // Rs7Log may throw (std::string allocation) inside a game frame
        Rs7Log("bass: low B bend held: %u reads, %u without a pitch (longest gap %.2f s), %u far too low; peak %.2f "
               "semitones",
               h.reads, h.missed, double(h.longestGap), h.low, double(h.peak));
    } catch (...) {
    }
}

// Play_GetDetector 0x4E8B50 without its channel-table walk: channel 0 / 2 -> [[135F57Ch] + 10h / 14h] + 4.
uint32_t DetectorOf(uint32_t channel) {
    const uint32_t root = Rs7Mid::Dword(0x135F57C);
    const uint32_t entry = root && (channel == 0 || channel == 2) ? Rs7Mid::Dword(root + (channel ? 0x14 : 0x10)) : 0;

    return entry ? Rs7Mid::Dword(entry + 4) : 0;
}
}  // namespace

void Rs7Pitch_ResetBends() {
    g_state.bendHold[0] = {};
    g_state.bendHold[1] = {};
}

extern "C" {
// The callees (bound by Rs7_Startup; the entries call them directly).
__declspec(dllexport) uintptr_t g_rs7_target_PC2 = 0, g_rs7_target_PC3 = 0, g_rs7_target_S4 = 0;

__declspec(dllexport) uintptr_t g_rs7_target_PC1 = 0, g_rs7_target_PC4 = 0, g_rs7_target_PBH = 0;

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

// PBH - 0x7E3F7D in Rnd_Note_UpdateInProgress 0x7E3700: the single-note bend read 0x4EA090 (ECX = channel [ebp-7Ch],
// push &semitones [ebp-44h], push tracker slot [ebp-80h]; AL = read ok, the caller pops 8). After placement, Full
// (floats). For string 4 (the low B, note data [ebp-78h] +10h) of an extended-bass song on its own detector,
// rs7bass::HoldBend bridges the pitch tracker's dropouts at the song time [ebp-5Ch] (bass.h). Else nothing changes.
__declspec(dllexport) void __cdecl Rs7_Mid_PBH(Context& c) {
    const uint32_t channel = Rs7Mid::Dword(c.ebp - 0x7C), data = Rs7Mid::Dword(c.ebp - 0x78);

    if (!data || *reinterpret_cast<const uint8_t*>(data + 0x10) != 4 || (channel != 0 && channel != 2) ||
        Rs7Bass_DetectorStrings(reinterpret_cast<const void*>(uintptr_t(DetectorOf(channel)))) < 5) {
        return;
    }

    float semis, now;

    std::memcpy(&semis, reinterpret_cast<const void*>(uintptr_t(c.ebp - 0x44)), 4);
    std::memcpy(&now, reinterpret_cast<const void*>(uintptr_t(c.ebp - 0x5C)), 4);
    rs7bass::BendHold& hold = g_state.bendHold[channel / 2];

    if (hold.note != reinterpret_cast<const void*>(uintptr_t(data))) {
        LogBend(hold);
    }
    const bool ok =
        rs7bass::HoldBend(hold, reinterpret_cast<const void*>(uintptr_t(data)), (c.eax & 0xFF) != 0, &semis, now);

    std::memcpy(reinterpret_cast<void*>(uintptr_t(c.ebp - 0x44)), &semis, 4);
    c.eax = (c.eax & ~0xFFu) | (ok ? 1u : 0u);
}
}
