#include "features/allocstats/allocstats.h"
#include "features/bass/bass.h"
#include "features/chords/chords.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/extras/extras.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/pitch/pitch_internal.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "model/instrument.h"
#include "model/slots.h"
#include "model/status.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <windows.h>

namespace {
using rs7p::UncheckedField;

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
        const bool bass = rs7::IsBassKind(UncheckedField<int>(det, rs7::kDetInstrument));
        const bool active = n == 7 && !bass;
        const Rs7Entry e{active, static_cast<int16_t>(active ? tuning->begin[6] : 0)};
        const uint32_t bassStrings = Rs7Bass_OnSong(det, tuning, UncheckedField<int>(det, rs7::kDetInstrument));

        Rs7Pitch_BassDetector(static_cast<uint8_t*>(det));

        Rs7Store_Set(static_cast<uint8_t*>(det) + rs7::kDetTuning, e);
        const int strings =
            int(bass ? (bassStrings ? bassStrings : rs7::kStockBassStrings) : rs7::GuitarStrings(size_t(n)));

        Rs7Status_SetSong(n, strings, bass, active, tuning->begin);  // overlay status panel
        Rs7Private_OnSongTuning(session, tuning->begin, n, bass);  // the private extras (src/features/extras/extras.h)
        Rs7Chord_OnSong();                                         // chord statistics, frame marks

        void* input = Rs7_SafeRead<void*>(det, rs7::kDetOwner);
        void* matcher = Rs7_SafeRead<void*>(input, rs7::kInputMatcher);
        const bool linked = matcher && Rs7_SafeRead<void*>(matcher, rs7::kMatcherDetector) == det;

        // Always overwrite a non-null matcher's key so a stale active entry (address reuse) cannot survive.
        if (matcher) {
            Rs7Store_Set(static_cast<uint8_t*>(matcher) + rs7::kMatcherTuning, linked ? e : Rs7Entry{false, 0});
        }

        Rs7Diag_Reset();
        Rs7Neck_ResetLog();
        Rs7S7_ResetLog();
        Rs7Frets_ResetLog();
        Rs7Pitch_ResetBends();
        Rs7Head_ResetLog();
        // The 7th string line is per guitar: this session's Rnd_Guitar (session+37Ch, the guitar
        // 0x460550 dims and 0x7DFB50 / 0x7E0040 start and stop). In native mode (C-lite + the publication ports) the
        // guitar's build count decides everything it shows and nothing is marked per song; the S4 mark only drives the
        // per-song rule while the ports are off at runtime.
        void* guitar = Rs7_SafeRead<void*>(session, rs7::kSessionGuitar);

        if (!g_rs7_native_publish) {
            Rs7S7_SetGuitarMode(guitar, active);
        }
        Rs7Song_WidenDetector(session, det, tuning);       // C-lite: the song's detector width (publish_state.h)
        Rs7Pitch_DetFloor(static_cast<uint8_t*>(det), 0);  // the floor follows the song's lowest open string
        InterlockedExchange(&g_rs7_any7, Rs7Store_AnyActive() ? 1 : 0);  // log/diagnostics only
        const uint8_t* ginfo = rs7::GuitarInfo(guitar);

        Rs7Log("song setup: session=%p detector=%p matcher=%p%s, %d tuning entries%s -> 7-string mode %s (t6=%d); "
               "guitar %p built with %d strings; extended bass %u strings",
               session, det, matcher, linked ? "" : " (not linked, not registered)", n, bass ? ", bass" : "",
               active ? "ON" : "off", e.t6, guitar, ginfo ? int(ginfo[rs7::kInfoStrings]) : -1, bassStrings);
        Rs7Ramp_LogStats();
        Rs7Alloc_Snapshot("song start");  // leak audit ([debug] alloc_stats)
    } catch (...) {
        // Never let an exception cross into the game frames.
    }
}

// Stateless: the side store's detector entry stays active after a 7-string song,
// while the tuner/Guitarcade reach this exit with a 6-entry vector and no S4. Calls nothing but the
// no-throw Rs7Pitch_DetTuned (native pitch storage only), so it cannot throw (and runs as-is in the
// emulator tests).
extern "C" void __cdecl Rs7_OnDetectorTuned(void* det, const Rs7Int16Vec* tuning) {
    // The native pitch storage: the detector's 8-entry tail (hooks.cpp).
    // Integer only, no throw; it lowers the lowest open MIDI exactly as below for 7 entries.
    if (g_rs7_native_pitch) {
        Rs7Pitch_DetTuned(static_cast<uint8_t*>(det), tuning);
    }
    const uintptr_t bytes = reinterpret_cast<uintptr_t>(tuning->end) - reinterpret_cast<uintptr_t>(tuning->begin);
    if (bytes != 7 * sizeof(int16_t) || rs7::IsBassKind(UncheckedField<int>(det, rs7::kDetInstrument))) {
        return;
    }

    int& lowest = UncheckedField<int>(det, rs7::kDetLowestOpen);

    lowest = rs7p::Lowest(lowest, tuning->begin, 7, false);  // 35 + t6 when lower
}

// Extended bass (features/bass/bass.h) on the detector the song bound: open MIDI of strings 4 / 5 (stock +1354h /
// +1356h keep the raw offsets; also the native tail's openMidi8) and the lowest open MIDI lowered to them (the raw
// floor follows it: Rs7Pitch_DetFloor). Integer only, no throw: PT1 / PR0 / S4.
extern "C" void __cdecl Rs7Pitch_BassDetector(uint8_t* det) {
    if (!det || !rs7::IsBassKind(UncheckedField<int>(det, rs7::kDetInstrument))) {
        return;
    }

    const uint32_t strings = Rs7Bass_DetectorStrings(det);

    if (!strings) {
        return;
    }

    const auto* tuning = &UncheckedField<int16_t>(det, rs7::kDetTuning);
    const bool tailed = Rs7Pitch_Tailed(det, 1) != 0;

    for (uint32_t s = rs7bass::kStockStrings; s < 6; ++s) {
        const int base = rs7bass::ExtraBase(s, strings);

        if (base == rs7bass::kNoBase) {
            continue;
        }

        const auto open = static_cast<int16_t>(base + tuning[s]);

        UncheckedField<int16_t>(det, rs7p::kDetOpen6 + 2 * s) = open;

        if (tailed) {
            UncheckedField<int16_t>(det, rs7p::kDetOpen8 + 2 * s) = open;
        }
    }

    int& lowest = UncheckedField<int>(det, rs7::kDetLowestOpen);

    lowest = rs7bass::Lowest(lowest, tuning, strings);
}
