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
// the hand-off at TS (Rs7Tuner8_Apply: prefix arrays -> capacity-8 tail), stock sessions on a 7/8-built guitar
// (Rs7Tuner8_EnsureNative) and
// what the shared hooks read back (wrong-instrument entries, labels). Game thread only.

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_tuner = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_tu8_labels[8] = {};

namespace {
using rs7tuner::g_state;
using rs7tuner::kSessions;
using rs7tuner::UncheckedField;
using rs7tuner::UncheckedRead;

// The InstrumentInfo of the guitar bound to `session` (+2Ch Rnd_Guitar -> +24h), or null. No SEH: callers guard.
const uint8_t* SessionInfo(const uint8_t* session) {
    const uint8_t* g = UncheckedRead<uint8_t*>(session, rs7tu::kGuitar);

    return g ? UncheckedRead<uint8_t*>(g, rs7tu::kGuitarInfo) : nullptr;
}

// Whether the session's guitar is a bass (InstrumentInfo +0 = 2); false without one.
bool GuitarIsBass(const uint8_t* session) {
    if (!session) {
        return false;
    }

    __try {
        const uint8_t* info = SessionInfo(session);

        return info && rs7::IsBassKind(UncheckedRead<uint8_t>(info, rs7::kInfoKind));
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return false;
    }
}
}  // namespace

void rs7tuner::Log(const char* fmt, const uint8_t* session, int a, int b, int c) {
    constexpr int kMaxLogs = 300;  // session lines per run

    if (g_state.sessionLogs >= kMaxLogs) {
        return;
    }

    ++g_state.sessionLogs;
    Rs7Log(fmt, session, a, b, c);
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

extern "C" int __cdecl Rs7Tuner8_HasSession(const uint8_t* session) {
    return SlotOf(session) >= 0 ? 1 : 0;
}

extern "C" void __cdecl Rs7Tuner8_InitSession(uint8_t* session) {
    if (!session) {
        return;
    }

    // Only TU8INIT calls this in the game, at the sole ctor caller's freshly allocated 380h owner.
    // Never probe beyond an unregistered stock owner.
    bool registered = Rs7Tuner8_HasSession(session) != 0;

    if (!registered) {
        for (int slot = 0; slot < kSessions; ++slot) {
            if (!g_state.sessions[slot]) {
                g_state.sessions[slot] = session;
                g_state.sessionFailed[slot] = false;
                g_state.pending[slot] = rs7tuner::Target{};
                registered = true;
                break;
            }
        }
    }

    if (!registered) {
        Rs7Log("tuner8: session registry exhausted - no tail initialized");
        return;
    }

    // The tail is unused (no reader) until TS makes the session native and fills it from the prefix.
    std::memset(session + rs7tu::kStockSize, 0, rs7tu::kSessionSize - rs7tu::kStockSize);

    for (int s = 0; s < 8; ++s) {
        reinterpret_cast<int32_t*>(session + rs7tu::kLastPick)[s] = -2;  // as 5AE0E9
    }

    *reinterpret_cast<uint32_t*>(session + rs7tu::kMagicOff) = rs7tu::kMagic;
}

extern "C" uint32_t __cdecl Rs7Tuner8_FreeSessionSize(uint8_t* session) {
    const int slot = SlotOf(session);

    if (slot >= 0) {
        g_state.sessions[slot] = nullptr;
        g_state.pending[slot] = rs7tuner::Target{};
        return rs7tu::kSessionSize;
    }

    return rs7tu::kStockSize;
}

// ---- native sessions ----
extern "C" int __cdecl Rs7Tuner8_IsNative(const uint8_t* session) {
    if (!Rs7Tuner8_HasSession(session)) {
        return 0;
    }

    return UncheckedRead<uint32_t>(session, rs7tu::kMagicOff) == rs7tu::kMagic &&
                   UncheckedRead<uint8_t>(session, rs7tu::kNative)
               ? 1
               : 0;
}

uint8_t rs7tuner::Count(const uint8_t* session) {
    return UncheckedRead<uint8_t>(session, rs7tu::kCount);
}

int rs7tuner::LowestOpen(const uint8_t* session) {
    const int n = Count(session);
    int low = 127;

    for (int s = 0; s < n && s < 8; ++s) {
        low = (std::min)(low, UncheckedRead<int>(session, rs7tu::kOpen + 4 * s));
    }

    return low;
}

void rs7tuner::SetLabels(const uint8_t* session) {  // game thread (SelectString, TS)
    const int slot = SlotOf(session);

    if (slot < 0) {
        return;
    }

    for (int s = 6; s < 8; ++s) {
        char* label = g_state.extraLabel[slot][s - 6];
        const int midi = UncheckedRead<int>(session, rs7tu::kOpen + 4 * s);

        std::snprintf(label, sizeof g_state.extraLabel[0][0], "%s", rs7tu::NoteName(midi));
        g_rs7_tu8_labels[s] = reinterpret_cast<uintptr_t>(label);
    }
}

int Rs7Tuner8_WrongEntries(Rs7WrongEnt* out, int max) {
    int n = 0;

    for (const uint8_t* s : g_state.sessions) {
        if (s && n < max && Rs7Tuner8_IsNative(s)) {
            out[n++] = Rs7WrongEnt{const_cast<uint8_t*>(s), rs7tuner::WrongThreshold(rs7tuner::LowestOpen(s))};
        }
    }

    return n;
}

namespace {
// Prefix -> tail: values copied, entity pointers moved (exactly one owner at any time).
void Migrate(uint8_t* session) {
    for (int s = 0; s < 6; ++s) {
        UncheckedField<int>(session, rs7tu::kOpen + 4 * s) = UncheckedRead<int>(session, rs7tu::kStockOpen + 4 * s);
        UncheckedField<int>(session, rs7tu::kOffsets + 4 * s) = UncheckedRead<int>(session, rs7tu::kStockOff + 4 * s);
        UncheckedField<float>(session, rs7tu::kPeaks + 4 * s) =
            UncheckedRead<float>(session, rs7tu::kStockPeaks + 4 * s);
        UncheckedField<int>(session, rs7tu::kLastPick + 4 * s) = UncheckedRead<int>(session, rs7tu::kStockLast + 4 * s);

        for (int k = 0; k < 2; ++k) {
            uint32_t& from = UncheckedField<uint32_t>(session, rs7tu::kStockLetters + 8 * s + 4 * k);

            UncheckedField<uint32_t>(session, rs7tu::kLetters + 8 * s + 4 * k) = from;
            from = 0;
        }

        uint32_t& pick = UncheckedField<uint32_t>(session, rs7tu::kStockPicks + 4 * s);

        UncheckedField<uint32_t>(session, rs7tu::kPicks + 4 * s) = pick;
        pick = 0;
    }

    for (int s = 6; s < 8; ++s) {
        UncheckedField<float>(session, rs7tu::kPeaks + 4 * s) = 0.0f;
        UncheckedField<int>(session, rs7tu::kLastPick + 4 * s) = -2;
        UncheckedField<uint32_t>(session, rs7tu::kLetters + 8 * s) = 0;
        UncheckedField<uint32_t>(session, rs7tu::kLetters + 8 * s + 4) = 0;
        UncheckedField<uint32_t>(session, rs7tu::kPicks + 4 * s) = 0;
    }
}

// Whether a session whose guitar is `guitar` may become native now: the copies read GuitarStrings main[8] (+40h)
// and its native count, and TS8PKS / TS8PKP carry the native FretGrid row stride (FGR sites). Logs a refusal.
bool CanGoNative(const uint8_t* session, const uint8_t* guitar, bool was, int n) {
    // A guitar bound to the session must have a native GuitarStrings (every one allocated with native_strings=1 has).
    if (guitar && !rs7gs::IsNative(UncheckedRead<uint8_t*>(guitar, rs7::kGuitarStrings)) && !was) {
        rs7tuner::Log("tuner8: session %p guitar %08X has no native GuitarStrings - %d-string target -> stock tuner",
                      session, int(reinterpret_cast<uintptr_t>(guitar)), n);
        return false;
    }

    // A session becomes native only after the deferred layout has been written (features/fretgrid/fretgrid.h
    // OnGuitarUpdate); until then it stays on the stock tuner.
    if (!was && rs7fg::RowStride() != rs7fg::kRowNative) {
        rs7tuner::Log("tuner8: session %p - FretGrid layout not written yet - %d-string target -> stock tuner", session,
                      n);
        return false;
    }

    return true;
}

// The open MIDI and offsets of all eight slots, the count, the order and the native flag. `bass`: an extended bass
// or a bass guitar (read by the caller before Migrate, so a fault there leaves the session untouched).
void WriteTarget(uint8_t* session, const int* offs, int n, const int extra[2], bool bass) {
    int open[8], off[8];

    rs7tu::Targets(offs, extra, open, off);

    if (n <= 6 && bass) {  // a bass target on a native session: E1 A1 D2 G2, the low B0 and the high C3
        // 0x1199CA4 (+ X_TBASS's B0 / C3)
        for (int s = 0; s < 6; ++s) {
            open[s] = rs7::kBassOpenMidi[s] + off[s];
        }
    }

    for (int s = 0; s < 8; ++s) {
        UncheckedField<int>(session, rs7tu::kOpen + 4 * s) = open[s];
        UncheckedField<int>(session, rs7tu::kOffsets + 4 * s) = off[s];
    }

    UncheckedField<uint8_t>(session, rs7tu::kCount) = uint8_t(n);
    rs7tu::Order(n, session + rs7tu::kOrder, bass);
    UncheckedField<uint8_t>(session, rs7tu::kNative) = 1;
}
}  // namespace

bool Rs7Tuner8_Apply(uint8_t* session, const int* offs, int n, const int extra[2], int bassStrings) {
    if (!session || !offs || (!g_rs7_native_tuner && !Rs7Tuner8_IsNative(session))) {
        return false;
    }

    __try {
        const int slot = SlotOf(session);

        if (slot < 0 || UncheckedRead<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            if (n > 6 || bassStrings) {
                rs7tuner::Log("tuner8: session %p is not a grown 380h session - %d-string target (extended bass %d) -> "
                              "stock tuner",
                              session, n, bassStrings);
            }

            return false;
        }

        const bool was = UncheckedRead<uint8_t>(session, rs7tu::kNative) != 0;
        auto& pending = g_state.pending[slot];

        pending = rs7tuner::Target{};
        const unsigned built = Rs7Tuner8_GuitarCount(session);
        // An extended bass: the song's keys (the guitar is often not bound yet when the target arrives) or the guitar.
        const bool builtBass = GuitarIsBass(session) && rs7::IsExtendedBass(built);
        const bool extBass = rs7::IsExtendedBass(bassStrings) || builtBass;

        if (extBass) {
            n = builtBass ? int(built) : bassStrings;  // a 5-string bass tunes five strings (six offsets in the target)
        }
        // A 6-string target on a stock session runs the game's code - unless the guitar is built with 7/8 strings
        // (the stock methods loop to InstrumentInfo +0Bh over six-slot arrays).
        if (!rs7tu::GoesNative(n, was, extBass ? unsigned(n) : built, extBass)) {
            return false;
        }

        if (n < 1 || n > 8) {
            return false;
        }

        if (!Rs7_SafeCopyZero(pending.offsets, offs, sizeof pending.offsets) ||
            !Rs7_SafeCopyZero(pending.extra, extra, sizeof pending.extra)) {
            return false;
        }

        pending.count = n;
        pending.bass = extBass ? n : 0;

        if (!CanGoNative(session, UncheckedRead<uint8_t*>(session, rs7tu::kGuitar), was, n)) {
            return false;
        }

        if (n <= 6 && !was) {
            rs7tuner::Log("tuner8: session %p - %d-string target on a guitar built with %d strings -> native session "
                          "(fix/menutuner)",
                          session, n, int(built));
        }

        const uint8_t* info = SessionInfo(session);
        const bool bass = extBass || (info && rs7::IsBassKind(UncheckedRead<uint8_t>(info, rs7::kInfoKind)));

        if (!was) {
            Migrate(session);
        }
        WriteTarget(session, offs, n, extra, bass);
        pending = rs7tuner::Target{};
        rs7tuner::SetLabels(session);
        Rs7Tuner_Refresh();
        rs7tuner::Log("tuner8: session %p is a native %d-string tuner session, first string %d, lowest open MIDI %d",
                      session, n, UncheckedRead<uint8_t>(session, rs7tu::kOrder), rs7tuner::LowestOpen(session));
        return true;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

unsigned Rs7Tuner8_GuitarCount(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    __try {
        const uint8_t* info = SessionInfo(session);

        return info ? UncheckedRead<uint8_t>(info, rs7tu::kInfoStrings) : 0u;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

bool Rs7Tuner8_EnsureNative(uint8_t* session, const char* where) {
    if (Rs7Tuner8_IsNative(session)) {
        return true;
    }
    if (!g_rs7_native_tuner || !session) {
        return false;
    }

    __try {
        const int slot = SlotOf(session);

        if (slot < 0 || UncheckedRead<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            return false;
        }

        const rs7tuner::Target pending = g_state.pending[slot];

        if (pending.count) {
            if (!Rs7Tuner8_Apply(session, pending.offsets, pending.count, pending.extra, pending.bass)) {
                return false;
            }

            if (UncheckedRead<uint32_t>(session, rs7tu::kMode) <= 3) {
                // A stock mode already selected string 0. Restart its check pass through the game's mode entry:
                // TS8F1 selects the new lowest string and SelectString repeats the tolerance / detector setup.
                using EnterCheckFn = void(__thiscall*)(uintptr_t, uint8_t*);

                reinterpret_cast<EnterCheckFn>(g_state.enterCheck)(0, session);
            }

            return true;
        }

        const unsigned built = Rs7Tuner8_GuitarCount(session);
        if (built <= 6) {
            return false;
        }

        int offs[6];

        for (int s = 0; s < 6; ++s) {
            offs[s] = UncheckedRead<int>(session, rs7tu::kStockOff + 4 * s);
        }

        const int extra[2] = {0, 0};

        if (Rs7Tuner8_Apply(session, offs, 6, extra)) {
            Rs7Log("tuner8: session %p was a stock session on a guitar built with %u strings - native before %s "
                   "(fix/menutuner)",
                   session, built, where ? where : "?");
            return true;
        }

        if (!g_state.sessionFailed[slot]) {  // game thread: one failure line per registered session
            g_state.sessionFailed[slot] = true;
            Rs7Log(
                "tuner8: session %p on a guitar built with %u strings could NOT be made native before %s - the stock "
                "six-slot tuner runs: REPORT THIS",
                session, built, where ? where : "?");
        }
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
    }

    return false;
}

// features/pitch/pitch.h: a profile re-apply keeps the song's extra strings while the native tuner is on.
int Rs7Pitch_KeepSongTuning() {
    return g_rs7_native_tuner ? 1 : 0;
}
