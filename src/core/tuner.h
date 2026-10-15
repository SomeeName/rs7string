#pragma once
#include <cstdint>

// The tuner's shared hooks (docs/ARCHITECTURE.md, "7th slot in game"; GitHub issues #1 and #8). Seven- and
// eight-string sessions run in the native 8-slot tuner session (core/tuner8.h); this file keeps what that session and
// the tuning names build on. The six-slot B-phase tuner (the 7th string as a phase of slot 0) was removed in 1.0.
//
// The arrangement manifest's Tuning object carries the 7th string's offset under the key
// "Rs7String6" (tools/manifest_key.py; never "string6": the song-list reader 0x56D0B0 overflows on
// it), the 8th string's under "Rs7String7". The pre-song tuner opens before song setup (S4), so these keys are the
// only early source.
//  - TK (Play_Tuning_ReadStringOffsets 0x5B8480 entry; its callers are the tuner menu
//    UI_TunerMenu_Enter, the MP/pre-song tuner menu 0x742D50 (both players) and 0x5B76C0): reads
//    the keys from the target tuning JSON (the game's own JSON vtable, as 0x5B84DC) and keeps them
//    for that offset array (menu+1B8h / +1D0h). The tuning names (TN) use them too (Rs7Tuner_SevenFor).
//  - TS (Play_TunerSession_ApplyTuning 0x5AF200 entry, session + that offset array): hands the session's
//    target (6, 7 or 8 strings) to the native session (Rs7Tuner8_Apply); without the native tuner, stock.
//  - W (UI_PlayerTuner_CheckWrongInstrument): the low-range threshold below.
//  - AT (UI_PlayerTuner_IsStringTuned 0x5B0C20 entry): [debug] tuner_autotune=N only (test
//    driving without a guitar): "tuned" N seconds after the session's first query. Default off.
// Every stub replays stock after one global read (g_rs7_tuner_any) while no native session or autotune exists.

extern "C" {
// [debug] tuner_autotune seconds (0 = off).
__declspec(dllexport) extern uintptr_t g_rs7_tuner_auto;
// Non-zero while some native tuner session exists or autotune is on: the stubs' fast stock path.
__declspec(dllexport) extern volatile long g_rs7_tuner_any;
// Game data (ref, never written).
__declspec(dllexport) extern uintptr_t g_rs7_tuner_mgr;  // X_TMGR  &0x135F5D4 (tuner sessions at +10h + slot*4)
// ClipFade (refs, never written), used by the native strings and pegs (gsnative.cpp, headstock.cpp).
__declspec(dllexport) extern uintptr_t g_rs7_fade_mgr;  // X_FADEMGR &ClipFade manager (0x1360480)
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_fadein;  // X_FADEIN  ClipFade_FadeInEntity 0x824E70 (EAX entity; mgr, s, 0, 1) ret 10h
__declspec(dllexport) extern uintptr_t g_rs7_fn_fadeout;  // X_FADEOUT ClipFade_FadeOutEntity 0x825000 (same)

// Low range ([tuner] lowrange, default 1; low-range research README "Demo-first hook plan", step 2):
// the guitar wrong-instrument test (W, UI_PlayerTuner_CheckWrongInstrument 0x5B2DDE) warns when the
// mean of ten detected notes is <= 34.0 (event 8), which trips on a correctly played A1/G1/F#1. For a
// 7-string tuner session the threshold becomes min(34, lowest open MIDI - 1); 6-string guitar, bass
// and sessions without Rs7String6 keep 34.0. The detector's lowest-open (+11F8h) is lowered to that
// lowest open note on each selection (the tuner reaches SetTuning with six entries, so S3 does not).
// The detector floor (MIDI 24, 0x4DBF73) stays: guitar needs no change, only a 5-string bass B0 does.
// TODO(bass B0): floor 22 + lowest 23 + corrected bass expected-pitch/base (docs/ARCHITECTURE.md).
// The threshold per live 7-string tuner session (rebuilt by every state change, so W reads plain data).
struct Rs7WrongEnt {
    uint8_t* session;
    int threshold;
};

__declspec(dllexport) extern Rs7WrongEnt g_rs7_wrong_tab[4];
__declspec(dllexport) extern uintptr_t g_rs7_lowrange;  // [tuner] lowrange
__declspec(dllexport) extern float g_rs7_wrong_thr;     // W: the session's threshold (written by Rs7_WrongThreshold)
__declspec(dllexport) extern float g_rs7_wrong_stock;   // W: 34.0f
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_getdet;  // X_GETDET Play_GetDetector 0x4E8B50 (ECX input id -> detector or 0)

// Stub entry points (cdecl, inside pushad).
// W: ESI = session, x87 live (ST0 = the mean): integer/SSE only, writes g_rs7_wrong_thr.
__declspec(dllexport) void __cdecl Rs7_WrongThreshold(uint8_t* session);
__declspec(dllexport) void __cdecl Rs7_TunerKey(int* offs, void* holder);              // TK
__declspec(dllexport) void __cdecl Rs7_TunerApply(uint8_t* session, const int* offs);  // TS
__declspec(dllexport) uint32_t __cdecl Rs7_TunerAutotune(uint8_t* session);            // AT: 1 = "tuned"
// Low range for a native tuner session (tuner8.h): the detector's lowest open lowered to lowestOpen.
void Rs7Tuner_LowerDetectorTo(const uint8_t* session, int lowestOpen);
}
// Rebuilds the wrong-instrument table and the stubs' fast-path flag from the native sessions (tuner8.h).
void Rs7Tuner_Refresh();

namespace rs7tuner {
constexpr char kKey[] = "Rs7String6";

// The guitar wrong-instrument threshold for a lowest open MIDI: min(34, lowest - 1).
int WrongThreshold(int lowestOpen);
}  // namespace rs7tuner

void Rs7Tuner_ResetAll();

// Tuning names (core/tuningname.h, TN): whether `offs` is an offset array TK read Rs7String6 for
// (the tuner menus' +1B8h / +1D0h, 0x5B76C0's stack array); *t6 = the 7th string's offset.
bool Rs7Tuner_SevenFor(const int* offs, int* t6);

// Overlay status: native tuner sessions now (those in the wrong-instrument table), g_rs7_tuner_any.
struct Rs7TunerStats {
    int sessions;
    bool any;
};

void Rs7Tuner_Stats(Rs7TunerStats* out);
