#pragma once
#include "features/tuner/tuner8s_ids.generated.h"
#include "features/tuner/tuningname.h"
#include "model/objtable.h"
#include <cstdint>

// The tuner feature's state (internal): everything its hooks keep between calls, in one place. The exported
// g_rsxr_* switches and game references stay separate globals (the stubs and the emulator tests address them by name).
// Game thread only, except the tuning-name log (interlocked) and calls (thread-local).

namespace rsxrtuner {
constexpr int kKeys = 4;       // TK offset arrays / autotune sessions kept at once
constexpr int kSessions = 32;  // registered sessions: four player slots, room for test owners and dtor-flag-0 objects
constexpr int kNameCalls = 8;  // open tuning-name calls (TJ / TU), a handful at most at a time
constexpr int kSessionCopies = RSXR_TU8S_COUNT;  // tools/gen_tuner8s.py

// TK results per offset array (menu+1B8h / +1D0h), consumed by TS and TN.
struct Key {
    bool found;
    int t6;
    double raw;
    bool found7 = false;  // RsxrString7 (the 8th string) next to RsxrString6
    int t7 = 0;
    int bass = 0;  // 5 / 6: RsxrString4 (+ RsxrString5) instead of RsxrString6, an extended bass
};

struct Auto {  // [debug] tuner_autotune per session
    uint8_t* session;
    uint32_t tick;
    int cur;
};

// An open tuning-name call (TJ / TU): Pre fills one, Post consumes it.
struct NameCall {
    bool used;
    uint32_t previous;
    void* raw;
    void* disp;  // TJ display output / TU output
    int cents;
    bool seven;  // TU: TUT saw a 7-string Tuning
    int t[7];
    int t8 = 0;
    bool eight = false;
    int bass = 0;  // 5 / 6: an extended-bass Tuning (RsxrString4 / RsxrString5), t[0..5] its offsets
};

struct Target {
    int count;
    int offsets[6];
    int extra[2];
    int bass;
};

struct NameCalls {
    NameCall entries[kNameCalls];
    uint32_t top;
};

extern thread_local NameCalls g_nameCalls;

struct State {
    // hooks.cpp
    // By offset array. A full table replaces its entries in turn: two players' tuners in a row must not share an
    // entry (co-op: P2's write evicted P1's key, P1's tuner lost its 7/8-string name).
    rsxr::ObjTable<const int*, Key, kKeys> keys;
    Auto autos[kKeys];
    bool guitarKeySeen;  // a guitar's target was read (TK), at guitarKeyTick
    uint32_t guitarKeyTick;
    // hooks_session.cpp: the registered grown sessions, the extra strings' labels ("B", "F#", ...) per registered
    // session (co-op: each player's tuner keeps its own), the log count, the "could not be made native" line
    // already logged per registered session
    const uint8_t* sessions[kSessions];
    char extraLabel[kSessions][2][4];
    int sessionLogs;
    bool sessionFailed[kSessions];
    Target pending[kSessions];  // a refused conversion retains the full TS target until its next method dispatch
    // hooks_methods.cpp: TS8SN1 -> TS8SN2 (game thread, one call)
    uint32_t soundString = 0xFFFFFFFF;
    const uint8_t* soundSession;
    // hooks_copies.cpp: GuitarStrings reader copies (TU8H / TU8C / TU8A) and session method copies (TS8*) with their
    // trampolines (the stock entry bytes + jmp back)
    uintptr_t readerCopy[3], readerTramp[3];
    uintptr_t sessCopy[kSessionCopies], sessTramp[kSessionCopies];
    // hooks_names.cpp: [tuner] names config, the log (each distinct line once: FNV-1a hashes of the last 64), open
    // calls live in g_nameCalls on each calling thread
    rsxrtn::Config names;
    volatile long nameLogs;
    uint32_t nameSeen[64];
    long nameSeenNext;
    uintptr_t enterCheck = 0x5AFAE0;  // mode 0 entry: ECX = submode, stack = session, ret 4
};

extern State g_state;
}  // namespace rsxrtuner
