// [debug] publish_trace=1: Rnd_Guitar_LoadStep 0x592A20 probes (sites LSA/LSG/LSC/LSE, diagnostic). Per guitar
// (ESI), counts calls and which step keeps the guitar "not ready" (1); logs at most once per 2 s per guitar,
// 60 lines in total. Game thread only.
#include "hooks/midhook.h"
#include "log.h"
#include <windows.h>
#include <cstdint>

namespace {
struct Probe {
    uint32_t guitar, calls, strings1, gridOk, before58dc1, headDone, final1;
    ULONGLONG last;
};

Probe g_p[4] = {};
volatile long g_lines = 0;

Probe& At(uint32_t guitar) {
    Probe* free = nullptr;

    for (auto& p : g_p) {
        if (p.guitar == guitar) {
            return p;
        }

        if (!p.guitar && !free) {
            free = &p;
        }
    }

    Probe& p = free ? *free : g_p[0];

    p = Probe{guitar};

    return p;
}
}  // namespace

static void Report(Probe& p, uint32_t guitar, const char* where, uint32_t result) {
    const ULONGLONG now = GetTickCount64();
    if (now - p.last < 2000) {
        return;
    }

    p.last = now;

    if (_InterlockedIncrement(&g_lines) <= 60) {
        Rs7Log(
            "loadstep: guitar %08X (%s %u) | calls %u: strings pending %u, grid done %u, pending before headstock %u, "
            "headstock done %u (pending %u), final pending %u",
            guitar, where, result, p.calls, p.strings1, p.gridOk, p.before58dc1, p.headDone, p.calls - p.headDone,
            p.final1);
    }
}

extern "C" void __cdecl Rs7_Mid_LSA(Rs7Mid::Context& c) {
    Probe& p = At(c.esi);

    ++p.calls;
    if (c.eax == 1) {
        ++p.strings1;
    }
    Report(p, c.esi, "strings", c.eax);
}

extern "C" void __cdecl Rs7_Mid_LSG(Rs7Mid::Context& c) {
    ++At(c.esi).gridOk;
}

extern "C" void __cdecl Rs7_Mid_LSC(Rs7Mid::Context& c) {
    if (c.edi == 1) {
        ++At(c.esi).before58dc1;
    }
}

extern "C" void __cdecl Rs7_Mid_LSE(Rs7Mid::Context& c) {
    Probe& p = At(c.esi);

    ++p.headDone;

    if (c.edi == 1) {
        ++p.final1;
    } else if (p.final1 || p.calls - p.headDone) {
        p.last = 0;
        Report(p, c.esi, "done, result", c.edi);
    }
}
