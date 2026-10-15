#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace {
CRITICAL_SECTION g_cs;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
FILE* g_file = nullptr;
DWORD g_start = 0;
Rs7LogSink g_sink = nullptr;
// The last kTail lines (no timestamp) for the overlay's log panel.
constexpr unsigned kTail = 200, kTailLen = 240;
char g_tail[kTail][kTailLen];
unsigned long g_tailSeq = 0;  // lines ever added

BOOL CALLBACK InitCs(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_cs);

    return TRUE;
}
}  // namespace

std::string Rs7Log_Format(unsigned ms, const char* msg) {
    char head[32];

    std::snprintf(head, sizeof head, "[%7u.%03u] ", ms / 1000, ms % 1000);

    return std::string(head) + msg + "\n";
}

void Rs7Log_Open(const std::wstring& path) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);

    if (!g_file) {
        g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);  // others may read it while the game runs
        g_start = GetTickCount();
    }

    LeaveCriticalSection(&g_cs);
}

void Rs7Log(const char* fmt, ...) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    // Formatted before taking the lock: if the allocation throws, the lock is not left held.
    const std::string line = Rs7Log_Format(GetTickCount() - g_start, msg);

    EnterCriticalSection(&g_cs);
    std::snprintf(g_tail[g_tailSeq % kTail], kTailLen, "%s", line.c_str());
    ++g_tailSeq;
    if (g_sink) {
        g_sink(msg);
    }

    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fflush(g_file);
    }

    LeaveCriticalSection(&g_cs);
}

void Rs7Log_SetSink(Rs7LogSink sink) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    g_sink = sink;
    LeaveCriticalSection(&g_cs);
}

unsigned long Rs7Log_Tail(char (*out)[240], unsigned max, unsigned* got) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    const unsigned long seq = g_tailSeq;
    const unsigned have = seq < kTail ? unsigned(seq) : kTail;
    const unsigned n = have < max ? have : max;

    for (unsigned i = 0; i < n; ++i) {
        const unsigned long idx = seq - n + i;

        std::memcpy(out[i], g_tail[idx % kTail], kTailLen);
    }

    LeaveCriticalSection(&g_cs);
    *got = n;

    return seq;
}
