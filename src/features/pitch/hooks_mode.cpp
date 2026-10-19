#include "features/allocstats/allocstats.h"
#include "features/chords/chords.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/vguit/vguit.h"
#include "model/status.h"
#include "platform/log.h"
#include <windows.h>

namespace {
template <class T>
T& At(void* base, uint32_t off) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

volatile LONG g_loggedFrets = 0;  // bit f set: string 6 fret f already logged this song
}  // namespace

extern "C" int __cdecl Rs7_Base6(const void* tuningKey) {
    try {
        Rs7Entry e;
        if (!Rs7Store_Get(tuningKey, &e) || !e.active) {
            return rs7::kNotActive;
        }

        return rs7::kLowB + e.t6;
    } catch (...) {
        return rs7::kNotActive;  // never let an exception cross into the game frames: stock behaviour
    }
}

extern "C" void __cdecl Rs7_OnSongTuning(void* session, void* det, const Rs7Int16Vec* tuning) {
    try {
        const int n = static_cast<int>(tuning->end - tuning->begin);
        const bool bass = At<int>(det, rs7::kDetInstrument) == 2;
        const bool active = n == 7 && !bass;
        const Rs7Entry e{active, static_cast<int16_t>(active ? tuning->begin[6] : 0)};

        Rs7Store_Set(static_cast<uint8_t*>(det) + rs7::kDetTuning, e);
        Rs7Status_SetSong(n, bass, active, tuning->begin);  // overlay status panel
        Rs7Vg_OnSongTuning(session, tuning->begin, n);      // [test] vguitar: independent expected MIDI
        Rs7Chord_OnSong(active, e.t6);                      // M4: chords with a string-6 column
        Rs7Chord_SetSongStrings(bass ? 4 : n);              // native_chordloops: 7/8-string chord frames

        void* input = At<void*>(det, rs7::kDetOwner);
        void* matcher = input ? At<void*>(input, rs7::kInputMatcher) : nullptr;
        const bool linked = matcher && At<void*>(matcher, rs7::kMatcherDetector) == det;

        // Always overwrite a non-null matcher's key so a stale active entry (address reuse) cannot survive.
        if (matcher) {
            Rs7Store_Set(static_cast<uint8_t*>(matcher) + rs7::kMatcherTuning, linked ? e : Rs7Entry{false, 0});
        }

        InterlockedExchange(&g_loggedFrets, 0);
        Rs7Diag_Reset();
        Rs7Neck_ResetLog();
        Rs7S7_ResetLog();
        Rs7Frets_ResetLog();
        Rs7Head_ResetLog();
        // The 7th string line (N5) is per guitar: this session's Rnd_Guitar (session+37Ch, the guitar
        // 0x460550 dims and 0x7DFB50 / 0x7E0040 start and stop). In native mode (C-lite + the publication ports) the
        // guitar's build count decides everything it shows and nothing is marked per song; the S4 mark only drives the
        // per-song rule while the ports are off at runtime.
        void* guitar = session ? At<void*>(session, rs7::kSessionGuitar) : nullptr;

        if (!g_rs7_native_publish) {
            Rs7S7_SetGuitarMode(guitar, active);
        }
        Rs7Song_WidenDetector(session, det, tuning);  // C-lite: the song's detector width (publish_state.h)
        InterlockedExchange(&g_rs7_any7, Rs7Store_AnyActive() ? 1 : 0);  // log/diagnostics only
        const uint8_t* ginfo = guitar ? At<const uint8_t*>(guitar, 0x24) : nullptr;

        Rs7Log("song setup: session=%p detector=%p matcher=%p%s, %d tuning entries%s -> 7-string mode %s (t6=%d); "
               "guitar %p built with %d strings",
               session, det, matcher, linked ? "" : " (not linked, not registered)", n, bass ? ", bass" : "",
               active ? "ON" : "off", e.t6, guitar, ginfo ? int(ginfo[0x0B]) : -1);
        Rs7Ramp_LogStats();
        Rs7Alloc_Snapshot("song start");  // leak audit ([debug] alloc_stats)
    } catch (...) {
        // Never let an exception cross into the game frames.
    }
}

// Stateless (final review F1): the side store's detector entry stays active after a 7-string song,
// while the tuner/Guitarcade reach this exit with a 6-entry vector and no S4. Calls nothing but the
// no-throw Rs7Pitch_DetTuned (native pitch storage only), so it cannot throw (and runs as-is in the
// emulator tests).
extern "C" void __cdecl Rs7_OnDetectorTuned(void* det, const Rs7Int16Vec* tuning) {
    // The native pitch storage (issue #18 stage 5 area 3): the detector's 8-entry tail (hooks.cpp).
    // Integer only, no throw; it lowers the lowest open MIDI exactly as below for 7 entries.
    if (g_rs7_native_pitch) {
        Rs7Pitch_DetTuned(static_cast<uint8_t*>(det), tuning);
    }
    const uintptr_t bytes = reinterpret_cast<uintptr_t>(tuning->end) - reinterpret_cast<uintptr_t>(tuning->begin);
    if (bytes != 7 * sizeof(int16_t) || At<int>(det, rs7::kDetInstrument) == 2) {
        return;
    }

    int& lowest = At<int>(det, rs7::kDetLowestOpen);
    const int low = rs7::kLowB + tuning->begin[6];

    if (low < lowest) {
        lowest = low;
    }
}

extern "C" void __cdecl Rs7_NoteString6(int fret, int t6) {
    try {
        if (fret < 0 || fret > 30) {
            return;
        }

        const LONG bit = LONG(1) << fret;
        if (InterlockedOr(&g_loggedFrets, bit) & bit) {
            return;
        }

        Rs7Log("string 6 fret %d: expected MIDI %d (35 + t6 %d + fret)", fret, rs7::kLowB + t6 + fret, t6);
    } catch (...) {
        // Called from the S2 stub inside game frames: Rs7Log may throw (std::string allocation).
    }
}
