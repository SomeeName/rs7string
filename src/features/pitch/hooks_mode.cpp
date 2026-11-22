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
using rsxrp::UncheckedField;

}  // namespace

extern "C" int __cdecl Rsxr_Base6(const void* tuningKey) {
    try {
        RsxrEntry e;
        if (!RsxrStore_Get(tuningKey, &e) || !e.active) {
            return rsxr::kNotActive;
        }

        return rsxr::kLowB + e.t6;
    } catch (...) {
        return rsxr::kNotActive;  // never let an exception cross into the game frames: stock behaviour
    }
}

extern "C" void __cdecl Rsxr_OnSongTuning(void* session, void* det, const RsxrInt16Vec* tuning) {
    try {
        const int n = static_cast<int>(tuning->end - tuning->begin);
        const bool bass = rsxr::IsBassKind(UncheckedField<int>(det, rsxr::kDetInstrument));
        const bool active = n == 7 && !bass;
        const RsxrEntry e{active, static_cast<int16_t>(active ? tuning->begin[6] : 0)};
        const uint32_t bassStrings = RsxrBass_OnSong(det, tuning, UncheckedField<int>(det, rsxr::kDetInstrument));

        RsxrPitch_BassDetector(static_cast<uint8_t*>(det));

        RsxrStore_Set(static_cast<uint8_t*>(det) + rsxr::kDetTuning, e);
        const int strings =
            int(bass ? (bassStrings ? bassStrings : rsxr::kStockBassStrings) : rsxr::GuitarStrings(size_t(n)));

        RsxrStatus_SetSong(n, strings, bass, active, tuning->begin);  // overlay status panel
        RsxrPrivate_OnSongTuning(session, tuning->begin, n, bass);  // the private extras (src/features/extras/extras.h)
        RsxrChord_OnSong();                                         // chord statistics, frame marks

        void* input = Rsxr_SafeRead<void*>(det, rsxr::kDetOwner);
        void* matcher = Rsxr_SafeRead<void*>(input, rsxr::kInputMatcher);
        const bool linked = matcher && Rsxr_SafeRead<void*>(matcher, rsxr::kMatcherDetector) == det;

        // Always overwrite a non-null matcher's key so a stale active entry (address reuse) cannot survive.
        if (matcher) {
            RsxrStore_Set(static_cast<uint8_t*>(matcher) + rsxr::kMatcherTuning, linked ? e : RsxrEntry{false, 0});
        }

        RsxrDiag_Reset();
        RsxrNeck_ResetLog();
        RsxrS7_ResetLog();
        RsxrFrets_ResetLog();
        RsxrPitch_ResetBends();
        RsxrHead_ResetLog();
        // The 7th string line is per guitar: this session's Rnd_Guitar (session+37Ch, the guitar
        // 0x460550 dims and 0x7DFB50 / 0x7E0040 start and stop). In native mode (C-lite + the publication ports) the
        // guitar's build count decides everything it shows and nothing is marked per song; the S4 mark only drives the
        // per-song rule while the ports are off at runtime.
        void* guitar = Rsxr_SafeRead<void*>(session, rsxr::kSessionGuitar);

        if (!g_rsxr_native_publish) {
            RsxrS7_SetGuitarMode(guitar, active);
        }
        RsxrSong_WidenDetector(session, det, tuning);       // C-lite: the song's detector width (publish_state.h)
        RsxrPitch_DetFloor(static_cast<uint8_t*>(det), 0);  // the floor follows the song's lowest open string
        InterlockedExchange(&g_rsxr_any7, RsxrStore_AnyActive() ? 1 : 0);  // log/diagnostics only
        const uint8_t* ginfo = rsxr::GuitarInfo(guitar);

        RsxrLog("song setup: session=%p detector=%p matcher=%p%s, %d tuning entries%s -> 7-string mode %s (t6=%d); "
                "guitar %p built with %d strings; extended bass %u strings",
                session, det, matcher, linked ? "" : " (not linked, not registered)", n, bass ? ", bass" : "",
                active ? "ON" : "off", e.t6, guitar, ginfo ? int(ginfo[rsxr::kInfoStrings]) : -1, bassStrings);
        RsxrRamp_LogStats();
        RsxrAlloc_Snapshot("song start");  // leak audit ([debug] alloc_stats)
    } catch (...) {
        // Never let an exception cross into the game frames.
    }
}

// Stateless: the side store's detector entry stays active after a 7-string song,
// while the tuner/Guitarcade reach this exit with a 6-entry vector and no S4. Calls nothing but the
// no-throw RsxrPitch_DetTuned (native pitch storage only), so it cannot throw (and runs as-is in the
// emulator tests).
extern "C" void __cdecl Rsxr_OnDetectorTuned(void* det, const RsxrInt16Vec* tuning) {
    // The native pitch storage: the detector's 8-entry tail (hooks.cpp).
    // Integer only, no throw; it lowers the lowest open MIDI exactly as below for 7 entries.
    if (g_rsxr_native_pitch) {
        RsxrPitch_DetTuned(static_cast<uint8_t*>(det), tuning);
    }
    const uintptr_t bytes = reinterpret_cast<uintptr_t>(tuning->end) - reinterpret_cast<uintptr_t>(tuning->begin);
    if (bytes != 7 * sizeof(int16_t) || rsxr::IsBassKind(UncheckedField<int>(det, rsxr::kDetInstrument))) {
        return;
    }

    int& lowest = UncheckedField<int>(det, rsxr::kDetLowestOpen);

    lowest = rsxrp::Lowest(lowest, tuning->begin, 7, false);  // 35 + t6 when lower
}

// Extended bass (features/bass/bass.h) on the detector the song bound: open MIDI of strings 4 / 5 (stock +1354h /
// +1356h keep the raw offsets; also the native tail's openMidi8) and the lowest open MIDI lowered to them (the raw
// floor follows it: RsxrPitch_DetFloor). Integer only, no throw: PT1 / PR0 / S4.
extern "C" void __cdecl RsxrPitch_BassDetector(uint8_t* det) {
    if (!det || !rsxr::IsBassKind(UncheckedField<int>(det, rsxr::kDetInstrument))) {
        return;
    }

    const uint32_t strings = RsxrBass_DetectorStrings(det);

    if (!strings) {
        return;
    }

    const auto* tuning = &UncheckedField<int16_t>(det, rsxr::kDetTuning);
    const bool tailed = RsxrPitch_Tailed(det, 1) != 0;

    for (uint32_t s = rsxrbass::kStockStrings; s < 6; ++s) {
        const int base = rsxrbass::ExtraBase(s, strings);

        if (base == rsxrbass::kNoBase) {
            continue;
        }

        const auto open = static_cast<int16_t>(base + tuning[s]);

        UncheckedField<int16_t>(det, rsxrp::kDetOpen6 + 2 * s) = open;

        if (tailed) {
            UncheckedField<int16_t>(det, rsxrp::kDetOpen8 + 2 * s) = open;
        }
    }

    int& lowest = UncheckedField<int>(det, rsxr::kDetLowestOpen);

    lowest = rsxrbass::Lowest(lowest, tuning, strings);
}
