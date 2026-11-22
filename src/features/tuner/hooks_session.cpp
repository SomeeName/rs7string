#include "features/fretgrid/fretgrid.h"
#include "features/pitch/pitch.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "model/instrument.h"
#include "model/tuning.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// The native 8-slot tuner session's lifetime and target: the registry of grown 380h sessions (TU8INIT / TU8FREE),
// the hand-off at TS (RsxrTuner8_Apply: prefix arrays -> capacity-8 tail), stock sessions on a 7/8-built guitar
// (RsxrTuner8_EnsureNative) and
// what the shared hooks read back (wrong-instrument entries, labels). Game thread only.

extern "C" __declspec(dllexport) uintptr_t g_rsxr_native_tuner = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_tu8_labels[8] = {};

namespace {
using rsxrtuner::g_state;
using rsxrtuner::kSessions;
using rsxrtuner::UncheckedField;
using rsxrtuner::UncheckedRead;

// The InstrumentInfo of the guitar bound to `session` (+2Ch Rnd_Guitar -> +24h), or null. No SEH: callers guard.
const uint8_t* SessionInfo(const uint8_t* session) {
    const uint8_t* g = UncheckedRead<uint8_t*>(session, rsxrtu::kGuitar);

    return g ? UncheckedRead<uint8_t*>(g, rsxrtu::kGuitarInfo) : nullptr;
}

// Whether the session's guitar is a bass (InstrumentInfo +0 = 2); false without one.
bool GuitarIsBass(const uint8_t* session) {
    if (!session) {
        return false;
    }

    __try {
        const uint8_t* info = SessionInfo(session);

        return info && rsxr::IsBassKind(UncheckedRead<uint8_t>(info, rsxr::kInfoKind));
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}
}  // namespace

void rsxrtuner::Log(const char* fmt, const uint8_t* session, int a, int b, int c) {
    constexpr int kMaxLogs = 300;  // session lines per run

    if (g_state.sessionLogs >= kMaxLogs) {
        return;
    }

    ++g_state.sessionLogs;
    RsxrLog(fmt, session, a, b, c);
}

// ---- the session registry ----
namespace {
// The registry slot of `session`, -1 when it is not registered.
int SlotOf(const uint8_t* session) {
    if (!session) {
        return -1;
    }

    for (int slot = 0; slot < kSessions; ++slot) {
        if (g_state.sessions[slot] == session) {
            return slot;
        }
    }

    return -1;
}
}  // namespace

extern "C" int __cdecl RsxrTuner8_HasSession(const uint8_t* session) {
    return SlotOf(session) >= 0 ? 1 : 0;
}

extern "C" void __cdecl RsxrTuner8_InitSession(uint8_t* session) {
    if (!session) {
        return;
    }

    // Only TU8INIT calls this in the game, at the sole ctor caller's freshly allocated 380h owner.
    // Never probe beyond an unregistered stock owner.
    bool registered = RsxrTuner8_HasSession(session) != 0;

    if (!registered) {
        for (int slot = 0; slot < kSessions; ++slot) {
            if (!g_state.sessions[slot]) {
                g_state.sessions[slot] = session;
                g_state.sessionFailed[slot] = false;
                g_state.pending[slot] = rsxrtuner::Target{};
                registered = true;
                break;
            }
        }
    }

    if (!registered) {
        RsxrLog("tuner8: session registry exhausted - no tail initialized");
        return;
    }

    // The tail is unused (no reader) until TS makes the session native and fills it from the prefix.
    std::memset(session + rsxrtu::kStockSize, 0, rsxrtu::kSessionSize - rsxrtu::kStockSize);

    for (int s = 0; s < 8; ++s) {
        reinterpret_cast<int32_t*>(session + rsxrtu::kLastPick)[s] = -2;  // as 5AE0E9
    }

    *reinterpret_cast<uint32_t*>(session + rsxrtu::kMagicOff) = rsxrtu::kMagic;
}

extern "C" uint32_t __cdecl RsxrTuner8_FreeSessionSize(uint8_t* session) {
    const int slot = SlotOf(session);

    if (slot >= 0) {
        g_state.sessions[slot] = nullptr;
        g_state.pending[slot] = rsxrtuner::Target{};
        return rsxrtu::kSessionSize;
    }

    return rsxrtu::kStockSize;
}

// ---- native sessions ----
extern "C" int __cdecl RsxrTuner8_IsNative(const uint8_t* session) {
    if (!RsxrTuner8_HasSession(session)) {
        return 0;
    }

    return UncheckedRead<uint32_t>(session, rsxrtu::kMagicOff) == rsxrtu::kMagic &&
                   UncheckedRead<uint8_t>(session, rsxrtu::kNative)
               ? 1
               : 0;
}

uint8_t rsxrtuner::Count(const uint8_t* session) {
    return UncheckedRead<uint8_t>(session, rsxrtu::kCount);
}

int rsxrtuner::LowestOpen(const uint8_t* session) {
    const int n = Count(session);
    int low = 127;

    for (int s = 0; s < n && s < 8; ++s) {
        low = (std::min)(low, UncheckedRead<int>(session, rsxrtu::kOpen + 4 * s));
    }

    return low;
}

void rsxrtuner::SetLabels(const uint8_t* session) {  // game thread (SelectString, TS)
    const int slot = SlotOf(session);

    if (slot < 0) {
        return;
    }

    for (int s = 6; s < 8; ++s) {
        char* label = g_state.extraLabel[slot][s - 6];
        const int midi = UncheckedRead<int>(session, rsxrtu::kOpen + 4 * s);

        std::snprintf(label, sizeof g_state.extraLabel[0][0], "%s", rsxrtu::NoteName(midi));
        g_rsxr_tu8_labels[s] = reinterpret_cast<uintptr_t>(label);
    }
}

int RsxrTuner8_WrongEntries(RsxrWrongEnt* out, int max) {
    int n = 0;

    for (const uint8_t* s : g_state.sessions) {
        if (s && n < max && RsxrTuner8_IsNative(s)) {
            out[n++] = RsxrWrongEnt{const_cast<uint8_t*>(s), rsxrtuner::WrongThreshold(rsxrtuner::LowestOpen(s))};
        }
    }

    return n;
}

namespace {
// Prefix -> tail: values copied, entity pointers moved (exactly one owner at any time).
void Migrate(uint8_t* session) {
    for (int s = 0; s < 6; ++s) {
        UncheckedField<int>(session, rsxrtu::kOpen + 4 * s) = UncheckedRead<int>(session, rsxrtu::kStockOpen + 4 * s);
        UncheckedField<int>(session, rsxrtu::kOffsets + 4 * s) = UncheckedRead<int>(session, rsxrtu::kStockOff + 4 * s);
        UncheckedField<float>(session, rsxrtu::kPeaks + 4 * s) =
            UncheckedRead<float>(session, rsxrtu::kStockPeaks + 4 * s);
        UncheckedField<int>(session, rsxrtu::kLastPick + 4 * s) =
            UncheckedRead<int>(session, rsxrtu::kStockLast + 4 * s);

        for (int k = 0; k < 2; ++k) {
            uint32_t& from = UncheckedField<uint32_t>(session, rsxrtu::kStockLetters + 8 * s + 4 * k);

            UncheckedField<uint32_t>(session, rsxrtu::kLetters + 8 * s + 4 * k) = from;
            from = 0;
        }

        uint32_t& pick = UncheckedField<uint32_t>(session, rsxrtu::kStockPicks + 4 * s);

        UncheckedField<uint32_t>(session, rsxrtu::kPicks + 4 * s) = pick;
        pick = 0;
    }

    for (int s = 6; s < 8; ++s) {
        UncheckedField<float>(session, rsxrtu::kPeaks + 4 * s) = 0.0f;
        UncheckedField<int>(session, rsxrtu::kLastPick + 4 * s) = -2;
        UncheckedField<uint32_t>(session, rsxrtu::kLetters + 8 * s) = 0;
        UncheckedField<uint32_t>(session, rsxrtu::kLetters + 8 * s + 4) = 0;
        UncheckedField<uint32_t>(session, rsxrtu::kPicks + 4 * s) = 0;
    }
}

// Whether a session whose guitar is `guitar` may become native now: the copies read GuitarStrings main[8] (+40h)
// and its native count, and TS8PKS / TS8PKP carry the native FretGrid row stride (FGR sites). Logs a refusal.
bool CanGoNative(const uint8_t* session, const uint8_t* guitar, bool was, int n) {
    // A guitar bound to the session must have a native GuitarStrings (every one allocated with native_strings=1 has).
    if (guitar && !rsxrgs::IsNative(UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarStrings)) && !was) {
        rsxrtuner::Log("tuner8: session %p guitar %08X has no native GuitarStrings - %d-string target -> stock tuner",
                       session, int(reinterpret_cast<uintptr_t>(guitar)), n);
        return false;
    }

    // A session becomes native only after the deferred layout has been written (features/fretgrid/fretgrid.h
    // OnGuitarUpdate); until then it stays on the stock tuner.
    if (!was && rsxrfg::RowStride() != rsxrfg::kRowNative) {
        rsxrtuner::Log("tuner8: session %p - FretGrid layout not written yet - %d-string target -> stock tuner",
                       session, n);
        return false;
    }

    return true;
}

// The open MIDI and offsets of all eight slots, the count, the order and the native flag. `bass`: an extended bass
// or a bass guitar (read by the caller before Migrate, so a fault there leaves the session untouched).
void WriteTarget(uint8_t* session, const int* offs, int n, const int extra[2], bool bass) {
    int open[8], off[8];

    rsxrtu::Targets(offs, extra, open, off);

    if (n <= 6 && bass) {  // a bass target on a native session: E1 A1 D2 G2, the low B0 and the high C3
        // 0x1199CA4 (+ X_TBASS's B0 / C3)
        for (int s = 0; s < 6; ++s) {
            open[s] = rsxr::kBassOpenMidi[s] + off[s];
        }
    }

    for (int s = 0; s < 8; ++s) {
        UncheckedField<int>(session, rsxrtu::kOpen + 4 * s) = open[s];
        UncheckedField<int>(session, rsxrtu::kOffsets + 4 * s) = off[s];
    }

    UncheckedField<uint8_t>(session, rsxrtu::kCount) = uint8_t(n);
    rsxrtu::Order(n, session + rsxrtu::kOrder, bass);
    UncheckedField<uint8_t>(session, rsxrtu::kNative) = 1;
}
}  // namespace

bool RsxrTuner8_Apply(uint8_t* session, const int* offs, int n, const int extra[2], int bassStrings) {
    if (!session || !offs || (!g_rsxr_native_tuner && !RsxrTuner8_IsNative(session))) {
        return false;
    }

    __try {
        const int slot = SlotOf(session);

        if (slot < 0 || UncheckedRead<uint32_t>(session, rsxrtu::kMagicOff) != rsxrtu::kMagic) {
            if (n > 6 || bassStrings) {
                rsxrtuner::Log(
                    "tuner8: session %p is not a grown 380h session - %d-string target (extended bass %d) -> "
                    "stock tuner",
                    session, n, bassStrings);
            }

            return false;
        }

        const bool was = UncheckedRead<uint8_t>(session, rsxrtu::kNative) != 0;
        auto& pending = g_state.pending[slot];

        pending = rsxrtuner::Target{};
        const unsigned built = RsxrTuner8_GuitarCount(session);
        // An extended bass: the song's keys (the guitar is often not bound yet when the target arrives) or the guitar.
        const bool builtBass = GuitarIsBass(session) && rsxr::IsExtendedBass(built);
        const bool extBass = rsxr::IsExtendedBass(bassStrings) || builtBass;

        if (extBass) {
            n = builtBass ? int(built) : bassStrings;  // a 5-string bass tunes five strings (six offsets in the target)
        }
        // A 6-string target on a stock session runs the game's code - unless the guitar is built with 7/8 strings
        // (the stock methods loop to InstrumentInfo +0Bh over six-slot arrays).
        if (!rsxrtu::GoesNative(n, was, extBass ? unsigned(n) : built, extBass)) {
            return false;
        }

        if (n < 1 || n > 8) {
            return false;
        }

        if (!Rsxr_SafeCopyZero(pending.offsets, offs, sizeof pending.offsets) ||
            !Rsxr_SafeCopyZero(pending.extra, extra, sizeof pending.extra)) {
            return false;
        }

        pending.count = n;
        pending.bass = extBass ? n : 0;

        if (!CanGoNative(session, UncheckedRead<uint8_t*>(session, rsxrtu::kGuitar), was, n)) {
            return false;
        }

        if (n <= 6 && !was) {
            rsxrtuner::Log("tuner8: session %p - %d-string target on a guitar built with %d strings -> native session "
                           "(fix/menutuner)",
                           session, n, int(built));
        }

        const uint8_t* info = SessionInfo(session);
        const bool bass = extBass || (info && rsxr::IsBassKind(UncheckedRead<uint8_t>(info, rsxr::kInfoKind)));

        if (!was) {
            Migrate(session);
        }
        WriteTarget(session, offs, n, extra, bass);
        pending = rsxrtuner::Target{};
        rsxrtuner::SetLabels(session);
        RsxrTuner_Refresh();
        rsxrtuner::Log("tuner8: session %p is a native %d-string tuner session, first string %d, lowest open MIDI %d",
                       session, n, UncheckedRead<uint8_t>(session, rsxrtu::kOrder), rsxrtuner::LowestOpen(session));
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

unsigned RsxrTuner8_GuitarCount(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    __try {
        const uint8_t* info = SessionInfo(session);

        return info ? UncheckedRead<uint8_t>(info, rsxrtu::kInfoStrings) : 0u;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

bool RsxrTuner8_EnsureNative(uint8_t* session, const char* where) {
    if (RsxrTuner8_IsNative(session)) {
        return true;
    }
    if (!g_rsxr_native_tuner || !session) {
        return false;
    }

    __try {
        const int slot = SlotOf(session);

        if (slot < 0 || UncheckedRead<uint32_t>(session, rsxrtu::kMagicOff) != rsxrtu::kMagic) {
            return false;
        }

        const rsxrtuner::Target pending = g_state.pending[slot];

        if (pending.count) {
            if (!RsxrTuner8_Apply(session, pending.offsets, pending.count, pending.extra, pending.bass)) {
                return false;
            }

            if (UncheckedRead<uint32_t>(session, rsxrtu::kMode) <= 3) {
                // A stock mode already selected string 0. Restart its check pass through the game's mode entry:
                // TS8F1 selects the new lowest string and SelectString repeats the tolerance / detector setup.
                using EnterCheckFn = void(__thiscall*)(uintptr_t, uint8_t*);

                reinterpret_cast<EnterCheckFn>(g_state.enterCheck)(0, session);
            }

            return true;
        }

        const unsigned built = RsxrTuner8_GuitarCount(session);
        if (built <= 6) {
            return false;
        }

        int offs[6];

        for (int s = 0; s < 6; ++s) {
            offs[s] = UncheckedRead<int>(session, rsxrtu::kStockOff + 4 * s);
        }

        const int extra[2] = {0, 0};

        if (RsxrTuner8_Apply(session, offs, 6, extra)) {
            RsxrLog("tuner8: session %p was a stock session on a guitar built with %u strings - native before %s "
                    "(fix/menutuner)",
                    session, built, where ? where : "?");
            return true;
        }

        if (!g_state.sessionFailed[slot]) {  // game thread: one failure line per registered session
            g_state.sessionFailed[slot] = true;
            RsxrLog(
                "tuner8: session %p on a guitar built with %u strings could NOT be made native before %s - the stock "
                "six-slot tuner runs: REPORT THIS",
                session, built, where ? where : "?");
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }

    return false;
}

// features/pitch/pitch.h: a profile re-apply keeps the song's extra strings while the native tuner is on.
int RsxrPitch_KeepSongTuning() {
    return g_rsxr_native_tuner ? 1 : 0;
}
