#include "features/window/window.h"
#include "platform/log.h"
#include "startup.h"
#include <windows.h>
#include <string>

namespace {
DWORD WINAPI Worker(LPVOID) {
    Rs7_Startup();

    return 0;
}
}  // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
        const std::wstring full(path, n);
        const std::wstring dir = full.substr(0, full.find_last_of(L"\\/") + 1);

        // [debug] log_redact=0 keeps user, profile path and computer name in the log (our own debugging only).
        Rs7Log_Open(dir + L"rs7string.log",
                    GetPrivateProfileIntW(L"debug", L"log_redact", 1, (dir + L"rs7string.ini").c_str()) != 0);
        // Here, not on the worker: the hooks must be in place before the game creates its window.
        Rs7Window_Install((dir + L"rs7string.ini").c_str());
        Rs7Console_Start((dir + L"rs7string.ini").c_str());  // own thread; [window] console_quickedit_off
        const HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);

        if (t) {
            CloseHandle(t);
        }
    }

    return TRUE;
}
