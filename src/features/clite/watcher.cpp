// C-lite rebuild timing: the log-only watcher thread (holder change -> load state 3) and the clock helpers.
#include "features/clite/clite_internal.h"
#include "model/instrument.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>

namespace rs7clite::internal {
using namespace rs7clite;
volatile long g_lines = 0;
volatile LONG64 g_selQpc = 0;          // QPC of the last GKT rebuild request (watcher: "since selection")
volatile long g_rebuilds[2] = {0, 0};  // per player: GKT rebuilds finished (EnsureForPlayer -> 1)
LARGE_INTEGER g_freq{};

double MsSince(LONG64 t0) {
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);

    return g_freq.QuadPart ? double(now.QuadPart - t0) * 1000.0 / double(g_freq.QuadPart) : 0.0;
}

LONG64 Now() {
    LARGE_INTEGER n;

    QueryPerformanceCounter(&n);

    return n.QuadPart;
}

Snap Read(uintptr_t mgr, unsigned slot) {
    Snap s;

    __try {
        s.holder = UncheckedRead<uintptr_t>(mgr + kMgrSlots + slot * 4);
        if (!s.holder) {
            return s;
        }

        s.guitar = UncheckedRead<uintptr_t>(s.holder + kHolderGuitar);
        const uintptr_t st = UncheckedRead<uintptr_t>(s.holder + kHolderState);

        s.state = st ? UncheckedRead<int>(st + kStateReady) : -1;
        const uintptr_t prof = UncheckedRead<uintptr_t>(s.holder + kHolderProfile);

        if (prof) {
            s.count = UncheckedRead<uint8_t>(prof + kProfileCount);
            s.type = UncheckedRead<int>(prof + kProfileType);
        }

        if (s.guitar) {  // the built InstrumentInfo's count (info +0Bh), when available
            const uintptr_t info = UncheckedRead<uintptr_t>(s.guitar + kGuitarInfo);

            if (info) {
                s.count =
                    s.count * 100 + UncheckedRead<uint8_t>(info + rs7::kInfoStrings);  // profile*100 + info (log only)
            }
        }
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        s.state = -2;
    }

    return s;
}

namespace {
DWORD WINAPI Watcher(void*) {
    Snap last[2];
    LONG64 born[2] = {0, 0};
    bool readyLogged[2] = {true, true};
    bool slowLogged[2] = {true, true};
    long seenRebuilds[2] = {0, 0};

    // Log only: once the clite log lines are used up there is nothing left to report.
    while (g_lines < kLogCap) {
        Sleep(5);
        const uintptr_t mgr = g_rs7_clite_mgr;
        if (!mgr || !g_rs7_clite) {
            continue;
        }

        // A GKT rebuild usually reuses the old guitar's addresses (rc-final3 run1: guitar 1BA645E0 for every build),
        // so a finished rebuild is a new guitar for the readiness log even when holder and guitar compare equal.
        for (unsigned i = 0; i < 2; ++i) {
            const long rebuilds = g_rebuilds[i];
            const bool rebuilt = rebuilds != seenRebuilds[i];

            seenRebuilds[i] = rebuilds;
            const Snap s = Read(mgr, i);
            // The identity of a rebuild is the guitar object (Rnd_Guitar address), not the holder or the profile: a
            // holder (and its profile) can stay while the guitar behind it is built anew.
            const bool holderNew = s.holder != last[i].holder;
            const bool guitarNew = (s.guitar && s.guitar != last[i].guitar) || (rebuilt && s.guitar);

            if ((holderNew || guitarNew) && s.holder) {
                slowLogged[i] = false;
            }

            if (s.holder && !readyLogged[i] && !slowLogged[i] && s.state != 3 && MsSince(born[i]) > 10000.0) {
                slowLogged[i] = true;  // diagnostic: a guitar that does not finish loading holds the song in state 1

                if (Rs7Log_Upto(&g_lines, kLogCap)) {
                    Rs7Log("clite: slot %u guitar %08X NOT ready 10 s after it appeared (state %d, count "
                           "profile*100+info %d)",
                           i, unsigned(s.guitar), s.state, s.count);
                }
            }

            if (holderNew || guitarNew) {
                if (holderNew || rebuilt || !born[i]) {
                    born[i] = Now();
                }
                readyLogged[i] = false;
                const LONG64 sel = g_selQpc;

                if (Rs7Log_Upto(&g_lines, kLogCap)) {
                    Rs7Log("clite: slot %u %s %08X (guitar %08X, profile type %d, count profile*100+info %d, "
                           "state %d)%s%.1f ms after the last clite request",
                           i, holderNew ? "new guitar holder" : "new guitar in holder", unsigned(s.holder),
                           unsigned(s.guitar), s.type, s.count, s.state, sel ? ", " : " - ", sel ? MsSince(sel) : 0.0);
                }
            }

            if (s.holder && !readyLogged[i] && s.state == 3) {
                readyLogged[i] = true;
                const LONG64 sel = g_selQpc;

                if (Rs7Log_Upto(&g_lines, kLogCap)) {
                    Rs7Log("clite: slot %u guitar %08X READY (state 3) %.1f ms after the holder appeared%s%.1f ms "
                           "after the last clite request; count profile*100+info %d, type %d",
                           i, unsigned(s.guitar), MsSince(born[i]), sel ? ", " : " - ", sel ? MsSince(sel) : 0.0,
                           s.count, s.type);
                }
            }

            last[i] = s;
        }
    }

    return 0;
}
}  // namespace

void StartWatcher() {
    QueryPerformanceFrequency(&g_freq);
    if (HANDLE h = CreateThread(nullptr, 0, &Watcher, nullptr, 0, nullptr)) {
        CloseHandle(h);
    }
}
}  // namespace rs7clite::internal
