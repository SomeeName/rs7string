#include "core/console.h"
#include "log.h"

DWORD Rs7Console_QuickEditOffMode(DWORD mode) {
    return (mode | ENABLE_EXTENDED_FLAGS) & ~DWORD(ENABLE_QUICK_EDIT_MODE);
}

namespace {
constexpr DWORD kPollMs = 250;
constexpr DWORD kDeadlineMs = 60000;

DWORD WINAPI Worker(LPVOID) {
    const DWORD t0 = GetTickCount();

    while (!GetConsoleWindow()) {
        if (GetTickCount() - t0 > kDeadlineMs) {
            Rs7Log("console: no console window within %u s - QuickEdit left alone", kDeadlineMs / 1000);
            return 0;
        }

        Sleep(kPollMs);
    }

    // CONIN$ is this process's console input buffer, whoever allocated the console.
    const HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);

    if (in == INVALID_HANDLE_VALUE) {
        Rs7Log("console: window %p after %u ms, CONIN$ not opened (error %lu) - QuickEdit left alone",
               static_cast<void*>(GetConsoleWindow()), GetTickCount() - t0, GetLastError());
        return 0;
    }

    DWORD mode = 0;

    if (!GetConsoleMode(in, &mode)) {
        Rs7Log("console: GetConsoleMode failed (error %lu) - QuickEdit left alone", GetLastError());
    } else {
        const DWORD want = Rs7Console_QuickEditOffMode(mode);
        const BOOL ok = want == mode || SetConsoleMode(in, want);
        DWORD now = 0;

        GetConsoleMode(in, &now);
        Rs7Log("console: window %p after %u ms: input mode %04lX -> %04lX (%s; QuickEdit %s)",
               static_cast<void*>(GetConsoleWindow()), GetTickCount() - t0, mode, now,
               ok ? "set" : "SetConsoleMode FAILED", (now & ENABLE_QUICK_EDIT_MODE) ? "ON" : "off");
    }

    CloseHandle(in);

    return 0;
}
}  // namespace

void Rs7Console_Start(const wchar_t* iniPath) {
    const bool on = GetPrivateProfileIntW(L"window", L"console_quickedit_off", 1, iniPath) != 0;

    Rs7Log("config: [window] console_quickedit_off=%d%s", on ? 1 : 0,
           on ? " -> QuickEdit off on the process console once it exists" : " -> console left stock");
    if (!on) {
        return;
    }

    // Own thread: it starts after DllMain returns (no console calls under the loader lock).
    const HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);

    if (t) {
        CloseHandle(t);
    }
}
