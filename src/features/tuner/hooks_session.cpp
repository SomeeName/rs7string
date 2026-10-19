#include "features/fretgrid/fretgrid.h"
#include "features/pitch/pitch.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "platform/log.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// The native 8-slot tuner session's lifetime and target: the registry of grown 380h sessions (TU8INIT / TU8FREE),
// the hand-off at TS (Rs7Tuner8_Apply: prefix arrays -> capacity-8 tail), fix/menutuner (Rs7Tuner8_EnsureNative) and
// what the shared hooks read back (wrong-instrument entries, labels). Game thread only.

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_tuner = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_tu8_labels[8] = {};

namespace {
using rs7tuner::At;
using rs7tuner::g_state;
using rs7tuner::Read;
}  // namespace

void rs7tuner::Log(const char* fmt, const uint8_t* session, int a, int b, int c) {
    if (g_state.sessionLogs >= 300) {
        return;
    }

    ++g_state.sessionLogs;
    Rs7Log(fmt, session, a, b, c);
}

// ---- the session registry ----
extern "C" int __cdecl Rs7Tuner8_HasSession(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    for (const uint8_t* ptr : g_state.sessions) {
        if (ptr == session) {
            return 1;
        }
    }

    return 0;
}

extern "C" void __cdecl Rs7Tuner8_InitSession(uint8_t* session) {
    if (!g_rs7_native_tuner || !session) {
        return;
    }

    // Only TU8INIT calls this in the game, at the sole ctor caller's freshly allocated 380h owner.
    // Never probe beyond an unregistered stock owner.
    bool registered = Rs7Tuner8_HasSession(session) != 0;

    if (!registered) {
        for (const uint8_t*& ptr : g_state.sessions) {
            if (!ptr) {
                ptr = session;
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
    for (const uint8_t*& ptr : g_state.sessions) {
        if (ptr && ptr == session) {
            ptr = nullptr;
            return rs7tu::kSessionSize;
        }
    }

    return rs7tu::kStockSize;
}

// ---- native sessions ----
extern "C" int __cdecl Rs7Tuner8_IsNative(const uint8_t* session) {
    if (!g_rs7_native_tuner || !Rs7Tuner8_HasSession(session)) {
        return 0;
    }

    return Read<uint32_t>(session, rs7tu::kMagicOff) == rs7tu::kMagic && Read<uint8_t>(session, rs7tu::kNative) ? 1 : 0;
}

uint8_t rs7tuner::Count(const uint8_t* session) {
    return Read<uint8_t>(session, rs7tu::kCount);
}

int rs7tuner::LowestOpen(const uint8_t* session) {
    const int n = Count(session);
    int low = 127;

    for (int s = 0; s < n && s < 8; ++s) {
        low = (std::min)(low, Read<int>(session, rs7tu::kOpen + 4 * s));
    }

    return low;
}

void rs7tuner::SetLabels(const uint8_t* session) {  // game thread (SelectString, TS)
    for (int s = 6; s < 8; ++s) {
        const int midi = Read<int>(session, rs7tu::kOpen + 4 * s);

        std::snprintf(g_state.extraLabel[s - 6], sizeof g_state.extraLabel[0], "%s", rs7tu::NoteName(midi));
        g_rs7_tu8_labels[s] = reinterpret_cast<uintptr_t>(g_state.extraLabel[s - 6]);
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
        At<int>(session, rs7tu::kOpen + 4 * s) = Read<int>(session, rs7tu::kStockOpen + 4 * s);
        At<int>(session, rs7tu::kOffsets + 4 * s) = Read<int>(session, rs7tu::kStockOff + 4 * s);
        At<float>(session, rs7tu::kPeaks + 4 * s) = Read<float>(session, rs7tu::kStockPeaks + 4 * s);
        At<int>(session, rs7tu::kLastPick + 4 * s) = Read<int>(session, rs7tu::kStockLast + 4 * s);

        for (int k = 0; k < 2; ++k) {
            uint32_t& from = At<uint32_t>(session, rs7tu::kStockLetters + 8 * s + 4 * k);

            At<uint32_t>(session, rs7tu::kLetters + 8 * s + 4 * k) = from;
            from = 0;
        }

        uint32_t& pick = At<uint32_t>(session, rs7tu::kStockPicks + 4 * s);

        At<uint32_t>(session, rs7tu::kPicks + 4 * s) = pick;
        pick = 0;
    }

    for (int s = 6; s < 8; ++s) {
        At<float>(session, rs7tu::kPeaks + 4 * s) = 0.0f;
        At<int>(session, rs7tu::kLastPick + 4 * s) = -2;
        At<uint32_t>(session, rs7tu::kLetters + 8 * s) = 0;
        At<uint32_t>(session, rs7tu::kLetters + 8 * s + 4) = 0;
        At<uint32_t>(session, rs7tu::kPicks + 4 * s) = 0;
    }
}

// Whether a session whose guitar is `guitar` may become native now: the copies read GuitarStrings main[8] (+40h)
// and its native count, and TS8PKS / TS8PKP carry the native FretGrid row stride (FGR sites). Logs a refusal.
bool CanGoNative(const uint8_t* session, const uint8_t* guitar, bool was, int n) {
    // A guitar bound to the session must have a native GuitarStrings (every one allocated with native_strings=1 has).
    if (guitar && !rs7gs::IsNative(Read<uint8_t*>(guitar, 0xC)) && !was) {
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

// The open MIDI and offsets of all eight slots, the count, the order and the native flag.
void WriteTarget(uint8_t* session, const int* offs, int n, const int extra[2]) {
    int open[8], off[8];

    rs7tu::Targets(offs, extra, open, off);
    uint8_t* guitar = Read<uint8_t*>(session, rs7tu::kGuitar);
    uint8_t* info = guitar ? Read<uint8_t*>(guitar, rs7tu::kGuitarInfo) : nullptr;

    if (n <= 6 && info && Read<uint8_t>(info, rs7tu::kInfoCount) == 4) {  // a bass target on a native session
        static const int kBassBase[6] = {28, 33, 38, 43, 0, 0};           // 0x1199CA4

        for (int s = 0; s < 6; ++s) {
            open[s] = kBassBase[s] + off[s];
        }
    }

    for (int s = 0; s < 8; ++s) {
        At<int>(session, rs7tu::kOpen + 4 * s) = open[s];
        At<int>(session, rs7tu::kOffsets + 4 * s) = off[s];
    }

    At<uint8_t>(session, rs7tu::kCount) = uint8_t(n);
    rs7tu::Order(n, session + rs7tu::kOrder);
    At<uint8_t>(session, rs7tu::kNative) = 1;
}
}  // namespace

bool Rs7Tuner8_Apply(uint8_t* session, const int* offs, int n, const int extra[2]) {
    if (!g_rs7_native_tuner || !session || !offs) {
        return false;
    }

    __try {
        if (!Rs7Tuner8_HasSession(session) || Read<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            if (n > 6) {
                rs7tuner::Log("tuner8: session %p is not a grown 380h session - %d-string target -> stock tuner",
                              session, n);
            }
            return false;
        }

        const bool was = Read<uint8_t>(session, rs7tu::kNative) != 0;
        const unsigned built = Rs7Tuner8_GuitarCount(session);
        // A 6-string target on a stock session runs the game's code - unless the guitar is built with 7/8 strings
        // (fix/menutuner: the stock methods loop to InstrumentInfo +0Bh over six-slot arrays).
        if (!rs7tu::GoesNative(n, was, built)) {
            return false;
        }

        if (n <= 6 && !was) {
            rs7tuner::Log("tuner8: session %p - %d-string target on a guitar built with %d strings -> native session "
                          "(fix/menutuner)",
                          session, n, int(built));
        }

        if (n < 1 || n > 8) {
            return false;
        }
        if (!CanGoNative(session, Read<uint8_t*>(session, rs7tu::kGuitar), was, n)) {
            return false;
        }

        if (!was) {
            Migrate(session);
        }
        WriteTarget(session, offs, n, extra);
        rs7tuner::SetLabels(session);
        Rs7Tuner_Refresh();
        rs7tuner::Log("tuner8: session %p is a native %d-string tuner session, first string %d, lowest open MIDI %d",
                      session, n, Read<uint8_t>(session, rs7tu::kOrder), rs7tuner::LowestOpen(session));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

unsigned Rs7Tuner8_GuitarCount(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    __try {
        const uint8_t* g = Read<uint8_t*>(session, rs7tu::kGuitar);
        const uint8_t* info = g ? Read<uint8_t*>(g, rs7tu::kGuitarInfo) : nullptr;

        return info ? Read<uint8_t>(info, rs7tu::kInfoCount) : 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool Rs7Tuner8_EnsureNative(uint8_t* session, const char* where) {
    if (!g_rs7_native_tuner || !session) {
        return false;
    }
    if (Rs7Tuner8_IsNative(session)) {
        return true;
    }

    static const uint8_t* s_failed = nullptr;  // game thread: one failure line per session

    __try {
        if (!Rs7Tuner8_HasSession(session) || Read<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            return false;
        }

        const unsigned built = Rs7Tuner8_GuitarCount(session);
        if (built <= 6) {
            return false;
        }

        int offs[6];

        for (int s = 0; s < 6; ++s) {
            offs[s] = Read<int>(session, rs7tu::kStockOff + 4 * s);
        }

        const int extra[2] = {0, 0};

        if (Rs7Tuner8_Apply(session, offs, 6, extra)) {
            Rs7Log("tuner8: session %p was a stock session on a guitar built with %u strings - native before %s "
                   "(fix/menutuner)",
                   session, built, where ? where : "?");
            return true;
        }

        if (s_failed != session) {
            s_failed = session;
            Rs7Log(
                "tuner8: session %p on a guitar built with %u strings could NOT be made native before %s - the stock "
                "six-slot tuner runs: REPORT THIS",
                session, built, where ? where : "?");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return false;
}

// features/pitch/pitch.h: a profile re-apply keeps the song's extra strings while the native tuner is on.
int Rs7Pitch_KeepSongTuning() {
    return g_rs7_native_tuner ? 1 : 0;
}
