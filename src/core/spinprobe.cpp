// [debug] spin_probe=1 (diagnostic): a watchdog thread that finds busy loops. Every 10 s it reads the CPU time
// of every thread of the process; a thread that used >= 8 s of CPU in that window is sampled 20 times
// (SuspendThread / GetThreadContext / ResumeThread, 25 ms apart): EIP and the first return addresses found
// above ESP are logged as module+offset (game addresses absolute). At most 2 reports per thread, 8 in total.
#include "core/spinprobe.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <tlhelp32.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
DWORD g_self = 0;

int Fmt(char* out, size_t cap, uint32_t v) {
    HMODULE m = nullptr;
    if (v < 0x10000 ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(uintptr_t(v)), &m)) {
        return std::snprintf(out, cap, " %08X", unsigned(v));
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(m);
    if (base == 0x400000) {
        return std::snprintf(out, cap, " game:%08X", unsigned(v));
    }
    if (base == reinterpret_cast<uintptr_t>(&__ImageBase)) {
        return std::snprintf(out, cap, " rs7:%08X", unsigned(v - base));
    }

    char path[MAX_PATH] = {};

    GetModuleFileNameA(m, path, MAX_PATH);
    const char* name = std::strrchr(path, '\\');

    return std::snprintf(out, cap, " %s:%08X", name ? name + 1 : path, unsigned(v - base));
}

bool LooksLikeReturn(uint32_t v) {
    HMODULE m = nullptr;
    if (v < 0x10000 ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(uintptr_t(v)), &m)) {
        return false;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(uintptr_t(v));

    __try {
        return p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
               (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One sample: EIP + up to 6 return addresses from the first 2 KB above ESP. The thread is suspended only while
// its context is read; the stack is read after it resumes (a moving stack only blurs the sample).
void Sample(HANDLE h, DWORD tid, int i) {
    CONTEXT c = {};

    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(h) == DWORD(-1)) {
        return;
    }

    const BOOL ok = GetThreadContext(h, &c);

    ResumeThread(h);
    if (!ok) {
        return;
    }

    char buf[640];
    int n = std::snprintf(buf, sizeof buf, "spin_probe: thread %lu sample %2d eip", tid, i);

    n += Fmt(buf + n, sizeof buf - n, c.Eip);
    n += std::snprintf(buf + n, sizeof buf - n, " eax %08lX ecx %08lX esi %08lX edi %08lX | ret", c.Eax, c.Ecx, c.Esi,
                       c.Edi);
    int found = 0;

    __try {
        for (int k = 0; k < 512 && found < 6 && n < 560; ++k) {
            const uint32_t v = reinterpret_cast<const uint32_t*>(uintptr_t(c.Esp))[k];
            if (!LooksLikeReturn(v)) {
                continue;
            }

            n += Fmt(buf + n, sizeof buf - n, v);
            ++found;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    Rs7Log("%s", buf);
}

uint64_t Cpu(HANDLE h) {
    FILETIME a, b, k, u;
    if (!GetThreadTimes(h, &a, &b, &k, &u)) {
        return 0;
    }

    return ((uint64_t(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
           ((uint64_t(u.dwHighDateTime) << 32) | u.dwLowDateTime);
}

DWORD WINAPI Probe(void*) {
    std::map<DWORD, uint64_t> last;
    std::map<DWORD, int> reports;
    int total = 0;
    const DWORD pid = GetCurrentProcessId();

    for (;;) {
        Sleep(10000);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            continue;
        }

        THREADENTRY32 te = {sizeof te};
        std::map<DWORD, uint64_t> now;

        for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == g_self) {
                continue;
            }

            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE,
                                  te.th32ThreadID);
            if (!h) {
                continue;
            }

            const uint64_t cpu = Cpu(h);

            now[te.th32ThreadID] = cpu;
            auto it = last.find(te.th32ThreadID);

            // >= 8 s of CPU (100 ns units) in the 10 s window: a busy loop
            if (it != last.end() && cpu - it->second >= 80000000ull && reports[te.th32ThreadID] < 2 && total < 8) {
                ++reports[te.th32ThreadID];
                ++total;
                Rs7Log("spin_probe: thread %lu used %.1f s CPU in the last 10 s - 20 samples:", te.th32ThreadID,
                       double(cpu - it->second) / 1e7);

                for (int i = 0; i < 20; ++i) {
                    Sample(h, te.th32ThreadID, i);
                    Sleep(25);
                }
            }

            CloseHandle(h);
        }

        CloseHandle(snap);
        last.swap(now);
    }
}
}  // namespace

void Rs7SpinProbe_Start() {
    HANDLE t = CreateThread(nullptr, 0, Probe, nullptr, CREATE_SUSPENDED, &g_self);
    if (!t) {
        return;
    }

    ResumeThread(t);
    CloseHandle(t);
    Rs7Log("debug: spin_probe=1 -> threads using >= 8 s CPU per 10 s are sampled (EIP + return addresses, diagnostic)");
}
