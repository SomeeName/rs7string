#include "features/songend/songend.h"
#include "features/allocstats/allocstats.h"
#include "features/scene/scene.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rs7_endlog = 1;

namespace {
constexpr unsigned kMaxLines = 400;  // per run; a song end costs 1-3 lines
volatile LONG g_lines;

// The session fields of .
struct Session {
    uint32_t vtable = 0, id = 0, song = 0;
    uint16_t state = 0, pending = 0;
    uint8_t flags = 0;
    float time = 0.0f;
    bool ok = false;
};

Session ReadSession(uint32_t s) {
    Session r;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(uintptr_t(s));

    r.ok = s && rs7s7d::SafeCopy(&r.vtable, p, 4) && rs7s7d::SafeCopy(&r.id, p + 8, 4) &&
           rs7s7d::SafeCopy(&r.song, p + 0x78, 4) && rs7s7d::SafeCopy(&r.state, p + 0x390, 2) &&
           rs7s7d::SafeCopy(&r.pending, p + 0x392, 2) && rs7s7d::SafeCopy(&r.flags, p + 0x394, 1) &&
           rs7s7d::SafeCopy(&r.time, p + 0x3B4, 4);

    return r;
}

// When each session last entered state 3 (playing): the end line says how long the song ran.
struct Start {
    uint32_t session;
    DWORD tick;
};

Start g_start[4];

void NoteStart(uint32_t s) {
    Start* slot = &g_start[0];

    for (Start& x : g_start) {
        if (x.session == s || !x.session) {
            slot = &x;
            break;
        }
    }

    *slot = Start{s, GetTickCount()};
}

double SecondsPlaying(uint32_t s) {
    for (const Start& x : g_start) {
        if (x.session == s) {
            return double(GetTickCount() - x.tick) / 1000.0;
        }
    }

    return -1.0;
}

bool InImage(uint32_t a) {
    static uint32_t lo, hi;

    if (!lo) {
        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);

        lo = uint32_t(uintptr_t(base)) + 0x1000;
        hi = uint32_t(uintptr_t(base)) + nt->OptionalHeader.SizeOfImage;
    }

    return a >= lo && a < hi;
}

bool IsReturn(uint32_t a) {
    uint8_t b[7];

    return InImage(a) && rs7s7d::SafeCopy(b, reinterpret_cast<const void*>(uintptr_t(a - 7)), 7) &&
           rs7end::EndsInCall(b);
}

// "frames A B C | stack D E F": the EBP chain from `ebp`, then return-address candidates on the stack
// from `sp` (a frame-pointer-less caller would be missing from the chain).
void Backtrace(uint32_t ebp, const uint32_t* sp, char* out, size_t n) {
    size_t at = std::snprintf(out, n, "frames");

    for (int i = 0; i < 10 && ebp && !(ebp & 3) && at < n; ++i) {
        uint32_t fr[2];
        if (!rs7s7d::SafeCopy(fr, reinterpret_cast<const void*>(uintptr_t(ebp)), 8)) {
            break;
        }
        if (!InImage(fr[1])) {
            break;
        }

        at += std::snprintf(out + at, n - at, " %08X", fr[1]);
        if (fr[0] <= ebp || fr[0] - ebp > 0x10000) {
            break;
        }

        ebp = fr[0];
    }

    if (at < n) {
        at += std::snprintf(out + at, n - at, " | stack");
    }
    int found = 0;

    for (int i = 0; i < 160 && found < 14 && at < n; ++i) {
        uint32_t v;
        if (!rs7s7d::SafeCopy(&v, sp + i, 4)) {
            break;
        }
        if (!IsReturn(v)) {
            continue;
        }

        ++found;
        at += std::snprintf(out + at, n - at, " +%X:%08X", unsigned(i * 4), v);
    }
}

bool Budget() {
    return InterlockedIncrement(&g_lines) <= LONG(kMaxLines);
}

void SessionText(const Session& s, char* out, size_t n) {
    std::snprintf(out, n, "vtable %08X id %u state %u pending %u +394h %02X song +78h %08X time %.2f s", s.vtable, s.id,
                  s.state, s.pending, s.flags, s.song, double(s.time));
}

void OnEvent(const Rs7Mid::Context& c) {  // SE1
    const auto* stack = reinterpret_cast<const uint32_t*>(c.esp);
    const uint32_t session = c.ecx;
    uint32_t ev[3] = {};
    if (!rs7s7d::SafeCopy(ev, reinterpret_cast<const void*>(uintptr_t(stack[1])), 12)) {
        return;
    }

    const Session s = ReadSession(session);
    if (!s.ok || !rs7end::IsEndEvent(ev[0], ev[1], s.id) || !Budget()) {
        return;
    }

    char st[160], bt[400];

    SessionText(s, st, sizeof st);
    Backtrace(c.ebp, stack, bt, sizeof bt);
    Rs7Log("song end [SE1 end-song event 0x7DD600]: session %08X event {target %u, type 3, flags %02X} %s, "
           "playing %.1f s | sender %08X | %s",
           session, ev[0], ev[2] & 0xFF, st, SecondsPlaying(session), stack[0], bt);
}

void OnLeave(const Rs7Mid::Context& c) {  // SE2
    const auto* stack = reinterpret_cast<const uint32_t*>(c.esp);
    const uint32_t session = c.esi, ret = stack[0];
    const unsigned old = stack[1] & 0xFFFF;
    const int to = rs7end::NewState(ret, c.edi, c.ebx);

    if (to == 3) {
        NoteStart(session);
    }
    if (old == 3) {
        Rs7Alloc_Snapshot("song end");  // leak audit ([debug] alloc_stats)
    }
    if (!Budget()) {
        return;
    }

    const Session s = ReadSession(session);
    char st[160];

    SessionText(s, st, sizeof st);

    if (old != 3) {
        Rs7Log("song state: session %08X leaves state %u -> %d (caller %08X %s) %s", session, old, to, ret,
               rs7end::CallerName(ret), st);
        return;
    }

    char bt[400];

    Backtrace(c.ebp, stack, bt, sizeof bt);
    Rs7Log(
        "song end [SE2 leaves state 3 'playing' 0x7DFA30] -> %d: session %08X, caller %08X %s, %s, playing %.1f s | %s",
        to, session, ret, rs7end::CallerName(ret), st, SecondsPlaying(session), bt);
}

void OnNoSong(const Rs7Mid::Context& c) {  // SE3
    const uint32_t session = c.esi;
    if (!Budget()) {
        return;
    }

    const Session s = ReadSession(session);
    char st[160], bt[400];

    SessionText(s, st, sizeof st);
    Backtrace(c.ebp, reinterpret_cast<const uint32_t*>(c.esp), bt, sizeof bt);
    Rs7Log("song end [SE3 song object +78h is NULL 0x7E0AE6]: session %08X %s, playing %.1f s | %s", session, st,
           SecondsPlaying(session), bt);
}

void OnSite(uint32_t site, const Rs7Mid::Context& c) {
    if (site == 1) {
        OnEvent(c);
    } else if (site == 2) {
        OnLeave(c);
    } else if (site == 3) {
        OnNoSong(c);
    }
}
}  // namespace

bool rs7end::IsEndEvent(uint32_t target, uint32_t type, uint32_t sessionId) {
    return type == 3 && (target == 2 || target == sessionId);
}

int rs7end::NewState(uint32_t ret, uint32_t edi, uint32_t ebx) {
    switch (ret) {
        case 0x7E0A3C:
            return int(edi & 0xFFFF);  // UpdateState: the pending request +392h
        case 0x7DD65E:
            return 4;  // Play_Session_OnEndSongEvent
        case 0x7DF85A:
            return int(edi & 0xFFFF);  // set state 0x7DF840 (DI)
        case 0x7EF06C:
        case 0x7EF0C8:
            return int(ebx & 0xFFFF);  // 0x7EEFC0 (BX)
        default:
            return -1;
    }
}

const char* rs7end::CallerName(uint32_t ret) {
    switch (ret) {
        case 0x7E0A3C:
            return "(UpdateState: pending request)";
        case 0x7DD65E:
            return "(end-song event)";
        case 0x7DF85A:
            return "(set state 0x7DF840)";
        case 0x7EF06C:
        case 0x7EF0C8:
            return "(sync 0x7EEFC0)";
        default:
            return "";
    }
}

bool rs7end::EndsInCall(const uint8_t b[7]) {
    if (b[2] == 0xE8) {
        return true;  // E8 rel32
    }
    if (b[5] == 0xFF && (b[6] & 0xF8) == 0xD0) {
        return true;  // call reg
    }
    if (b[5] == 0xFF && (b[6] & 0xF8) == 0x10 && (b[6] & 7) != 4 && (b[6] & 7) != 5) {
        return true;  // [reg]
    }
    if (b[4] == 0xFF && b[5] == 0x14) {
        return true;  // [sib]
    }
    if (b[4] == 0xFF && (b[5] & 0xF8) == 0x50 && (b[5] & 7) != 4) {
        return true;  // [reg+d8]
    }
    if (b[3] == 0xFF && b[4] == 0x54) {
        return true;  // [sib+d8]
    }
    if (b[1] == 0xFF && ((b[2] & 0xF8) == 0x90 && (b[2] & 7) != 4)) {
        return true;  // [reg+d32]
    }
    if (b[1] == 0xFF && b[2] == 0x15) {
        return true;  // [d32]
    }
    if (b[0] == 0xFF && b[1] == 0x94) {
        return true;  // [sib+d32]
    }

    return false;
}

unsigned rs7end::Lines() {
    return unsigned(g_lines);
}

extern "C" __declspec(dllexport) void __cdecl Rs7End_OnSite(uint32_t site, const uint32_t* f) {
    // Compatibility for the former PUSHAD ABI. The saved ESP slot is after
    // PUSHFD; the actual live stack starts immediately after the nine dwords.
    __try {
        if (!g_rs7_endlog) {
            return;
        }

        Rs7Mid::Context c{};

        std::memcpy(&c, f, 9 * sizeof(uint32_t));
        c.esp = uint32_t(uintptr_t(f + 9));
        Rs7End_OnContext(site, c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_endlog = 0;
        Rs7Log("song end log: fault %08lX at site SE%u - switched off for this run", GetExceptionCode(), site);
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7End_OnContext(uint32_t site, const Rs7Mid::Context& c) {
    __try {
        if (g_rs7_endlog) {
            OnSite(site, c);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_endlog = 0;
        Rs7Log("song end log: fault %08lX at site SE%u - switched off for this run", GetExceptionCode(), site);
    }
}
