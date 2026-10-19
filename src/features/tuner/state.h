#pragma once
#include "features/tuner/tuner8s_ids.generated.h"
#include "features/tuner/tuningname.h"
#include <cstdint>

// The tuner feature's state (internal): everything its hooks keep between calls, in one place. The exported
// g_rs7_* switches and game references stay separate globals (the stubs and the emulator tests address them by name).
// Game thread only, except the tuning-name calls (several threads; interlocked).

namespace rs7tuner {
constexpr int kKeys = 4;       // TK offset arrays / autotune sessions kept at once
constexpr int kSessions = 32;  // registered sessions: four player slots, room for test owners and dtor-flag-0 objects
constexpr int kNameCalls = 8;  // open tuning-name calls (TJ / TU), a handful at most at a time
constexpr int kSessionCopies = RS7_TU8S_COUNT;  // tools/gen_tuner8s.py

// TK results per offset array (menu+1B8h / +1D0h), consumed by TS and TN.
struct Key {
    const int* offs;
    bool found;
    int t6;
    double raw;
    bool found7 = false;  // Rs7String7 (the 8th string) next to Rs7String6
    int t7 = 0;
};

struct Auto {  // [debug] tuner_autotune per session
    uint8_t* session;
    uint32_t tick;
    int cur;
};

// An open tuning-name call (TJ / TU): Pre fills one, Post consumes it.
struct NameCall {
    volatile long used;
    unsigned long thread;
    void* raw;
    void* disp;  // TJ display output / TU output
    int cents;
    bool seven;  // TU: TUT saw a 7-string Tuning
    int t[7];
    int t8 = 0;
    bool eight = false;
};

struct State {
    // hooks.cpp
    Key keys[kKeys];
    Auto autos[kKeys];
    // hooks_session.cpp: the registered grown sessions, the extra strings' labels ("B", "F#", ...), the log count
    const uint8_t* sessions[kSessions];
    char extraLabel[2][4];
    int sessionLogs;
    // hooks_methods.cpp: TS8SN1 -> TS8SN2 (game thread, one call)
    uint32_t soundString = 0xFFFFFFFF;
    const uint8_t* soundSession;
    // hooks_copies.cpp: GuitarStrings reader copies (TU8H / TU8C / TU8A) and session method copies (TS8*) with their
    // trampolines (the stock entry bytes + jmp back)
    uintptr_t readerCopy[3], readerTramp[3];
    uintptr_t sessCopy[kSessionCopies], sessTramp[kSessionCopies];
    // hooks_names.cpp: [tuner] names config, the log (each distinct line once: FNV-1a hashes of the last 64), open
    // calls
    rs7tn::Config names;
    volatile long nameLogs;
    uint32_t nameSeen[64];
    long nameSeenNext;
    NameCall calls[kNameCalls];
};

extern State g_state;
}  // namespace rs7tuner
