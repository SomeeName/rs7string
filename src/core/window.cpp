#include "core/window.h"
#include "core/protect.h"
#include "log.h"
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <vector>

POINT Rs7Window_Centre(const RECT& work, int w, int h) {
    LONG x = work.left + (work.right - work.left - w) / 2;
    LONG y = work.top + (work.bottom - work.top - h) / 2;

    if (x < work.left) {
        x = work.left;
    }
    if (y < work.top) {
        y = work.top;
    }

    return POINT{x, y};
}

int Rs7Window_NoActivateCmd(int cmd) {
    switch (cmd) {
        case SW_SHOWNORMAL:
        case SW_SHOWMAXIMIZED:
        case SW_RESTORE:
        case SW_SHOWDEFAULT:
            return SW_SHOWNOACTIVATE;
        case SW_SHOW:
            return SW_SHOWNA;
        case SW_SHOWMINIMIZED:
            return SW_SHOWMINNOACTIVE;
        default:
            return cmd;
    }
}

Rs7Window_MessageFilter Rs7Window_FilterMessage(UINT m, WPARAM w, LPARAM, bool background) {
    if (!background) {
        return {false, 0};
    }

    switch (m) {
        case WM_ACTIVATEAPP:
            return {w == FALSE, 0};
        case WM_ACTIVATE:
            return {LOWORD(w) == WA_INACTIVE, 0};
        case WM_KILLFOCUS:
            return {true, 0};
        // TRUE lets Windows complete the change of active window. We only hide the notification
        // from the game; returning FALSE here could prevent the owner from switching apps.
        case WM_NCACTIVATE:
            return {w == FALSE, w == FALSE ? TRUE : 0};
        default:
            break;
    }

    return {false, 0};
}

namespace {
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using HwndBool_t = BOOL(WINAPI*)(HWND);
using HwndHwnd_t = HWND(WINAPI*)(HWND);

CreateWindowExA_t g_create;
ShowWindow_t g_show;
MoveWindow_t g_move;
SetWindowPos_t g_setPos;
HwndBool_t g_setFg, g_toTop;
HwndHwnd_t g_setActive, g_setFocus;
using GetFocus_t = HWND(WINAPI*)();
GetFocus_t g_getFocus;
GetFocus_t g_getForeground;
uintptr_t g_gameBegin, g_gameEnd;

bool g_background;
int g_screen;
HWND g_main;
WNDPROC g_prevProc;
volatile LONG g_logBudget = 24;

void LogLimited(const char* fmt, int a, int b, int c, int d) {
    if (InterlockedDecrement(&g_logBudget) >= 0) {
        Rs7Log(fmt, a, b, c, d);
    }
}

BOOL CALLBACK CollectMonitor(HMONITOR m, HDC, LPRECT, LPARAM p) {
    MONITORINFO mi{sizeof(mi)};

    if (GetMonitorInfoW(m, &mi)) {
        auto& v = *reinterpret_cast<std::vector<MONITORINFO>*>(p);

        if (mi.dwFlags & MONITORINFOF_PRIMARY) {
            v.insert(v.begin(), mi);
        } else {
            v.push_back(mi);
        }
    }

    return TRUE;
}

// The work area of monitor g_screen, or false (screen=0 or no such monitor).
bool TargetWork(RECT* work) {
    if (g_screen <= 0) {
        return false;
    }

    std::vector<MONITORINFO> mons;

    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&mons));
    if (g_screen > int(mons.size())) {
        return false;
    }

    *work = mons[size_t(g_screen) - 1].rcWork;

    return true;
}

// Always use the real query here: the game's view must not disable our activation blockers.
bool Quiet(HWND h) {
    return g_background && h && h == g_main && g_getForeground() != g_main;
}

LRESULT CALLBACK BackgroundWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const WNDPROC prev = g_prevProc;
    const auto filtered = Rs7Window_FilterMessage(m, w, l, g_background && h == g_main);

    if (filtered.consume) {
        LogLimited("window: suppressed focus loss msg=%04X w=%08X l=%08X result=%d", int(m), int(w), int(l),
                   int(filtered.result));
        return filtered.result;
    }

    const LRESULT result = CallWindowProcA(prev, h, m, w, l);

    if (m == WM_NCDESTROY && h == g_main) {
        g_main = nullptr;
        g_prevProc = nullptr;
    }

    return result;
}

void Place(int* x, int* y, int w, int h2) {
    RECT work;
    if (!TargetWork(&work)) {
        return;
    }

    const POINT p = Rs7Window_Centre(work, w, h2);

    LogLimited("window: %d,%d -> %d,%d (screen setting)", *x, *y, int(p.x), int(p.y));
    *x = p.x;
    *y = p.y;
}

HWND WINAPI CreateWindowExAHook(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent,
                                HMENU menu, HINSTANCE inst, LPVOID param) {
    const bool candidate = !g_main && !parent && !(style & WS_CHILD) && (style & WS_CAPTION) == WS_CAPTION;

    if (candidate) {
        Place(&x, &y, w, h);
    }
    const HWND r = g_create(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);

    if (candidate && r) {
        g_main = r;

        // Independent of the overlay's later subclass: background driving also works with
        // overlay.enabled=0, and losses between creation and the first Present are hidden.
        if (g_background) {
            g_prevProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(r, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BackgroundWndProc)));
            if (g_prevProc) {
                Rs7Log("window: background focus filter installed");
            } else {
                Rs7Log("window: background focus filter FAILED (%lu)", GetLastError());
            }
        }

        LogLimited("window: game window created (%dx%d at %d,%d)", w, h, x, y);
    }

    return r;
}

BOOL WINAPI ShowWindowHook(HWND h, int cmd) {
    if (Quiet(h)) {
        const int c = Rs7Window_NoActivateCmd(cmd);

        if (c != cmd) {
            LogLimited("window: ShowWindow %d -> %d (background)", cmd, c, 0, 0);
        }
        cmd = c;
    }

    return g_show(h, cmd);
}

BOOL WINAPI MoveWindowHook(HWND h, int x, int y, int w, int h2, BOOL repaint) {
    if (h && h == g_main) {
        Place(&x, &y, w, h2);
    }

    return g_move(h, x, y, w, h2, repaint);
}

BOOL WINAPI SetWindowPosHook(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (h && h == g_main) {
        if (Quiet(h)) {
            flags |= SWP_NOACTIVATE;
        }
        // A resize in place (the game grows the window to its resolution with SWP_NOMOVE) would
        // keep the old top-left and reach into the neighbouring monitor: re-centre it as well.
        if (!(flags & SWP_NOSIZE) && (flags & SWP_NOMOVE) && g_screen > 0) {
            flags &= ~SWP_NOMOVE;
        }

        if (!(flags & SWP_NOMOVE)) {
            int w = cx, ht = cy;

            if (flags & SWP_NOSIZE) {
                RECT r;

                GetWindowRect(h, &r);
                w = r.right - r.left;
                ht = r.bottom - r.top;
            }

            Place(&x, &y, w, ht);
        }
    }

    return g_setPos(h, after, x, y, cx, cy, flags);
}

BOOL WINAPI SetForegroundWindowHook(HWND h) {
    if (Quiet(h)) {
        return TRUE;
    }

    return g_setFg(h);
}

BOOL WINAPI BringWindowToTopHook(HWND h) {
    if (Quiet(h)) {
        return TRUE;
    }

    return g_toTop(h);
}

HWND WINAPI SetActiveWindowHook(HWND h) {
    if (Quiet(h)) {
        return GetActiveWindow();
    }

    return g_setActive(h);
}

HWND WINAPI SetFocusHook(HWND h) {
    if (h && Quiet(GetAncestor(h, GA_ROOT))) {
        return GetFocus();
    }

    return g_setFocus(h);
}

// The game runs its per-frame input-device dispatch (0x9B1D40) only when GetFocus() (its only
// call, 0x9B1D5A) returns a window; a never-activated window gets NULL there. Report the game
// window instead, on the game window's thread only, so the input layer runs in the background.
// The real keyboard cannot leak in: Rs7Dinput holds the game's keyboard to DISCL_FOREGROUND.
HWND WINAPI GetFocusHook() {
    const HWND r = g_getFocus();
    if (!r && g_background && g_main && GetWindowThreadProcessId(g_main, nullptr) == GetCurrentThreadId()) {
        return g_main;
    }

    return r;
}

HWND WINAPI GetForegroundWindowHook() {
    // CONFIRMED game call: 0xEC5D9D (resolved VMProtect import). Limit the lie to the game
    // window thread AND executable callers; overlay/user32/DirectInput must see real focus.
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (g_background && g_main && caller >= g_gameBegin && caller < g_gameEnd &&
        GetWindowThreadProcessId(g_main, nullptr) == GetCurrentThreadId()) {
        return g_main;
    }

    return g_getForeground();
}

bool Write(uint8_t* at, const uint8_t* bytes, size_t n) {
    DWORD old = 0;
    if (!Rs7_Protect(at, n, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }

    std::memcpy(at, bytes, n);
    Rs7_Protect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);

    return true;
}

// Redirects the export to `hook`; returns the address that runs the original, or nullptr.
// Three shapes in SysWOW64\user32.dll (Windows 11): "jmp [win32u slot]" (6 bytes), the
// hot-patch prologue "mov edi,edi" with 5 bytes of padding before the function, and
// "mov eax, fs:[18h]" (GetFocus: 6 position-independent bytes, copied to a trampoline).
void* HookExport(HMODULE m, const char* name, void* hook) {
    auto f = reinterpret_cast<uint8_t*>(GetProcAddress(m, name));
    if (!f) {
        return nullptr;
    }

    if (f[0] == 0xFF && f[1] == 0x25) {
        void* orig = **reinterpret_cast<void***>(f + 2);
        uint8_t j[6] = {0xE9, 0, 0, 0, 0, 0x90};
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(f + 5));

        std::memcpy(j + 1, &rel, 4);
        return Write(f, j, sizeof(j)) ? orig : nullptr;
    }

    if (f[0] == 0x8B && f[1] == 0xFF) {
        for (int i = 1; i <= 5; ++i) {
            if (f[-i] != 0xCC && f[-i] != 0x90) {
                return nullptr;
            }
        }

        uint8_t j[5] = {0xE9};
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(f));

        std::memcpy(j + 1, &rel, 4);
        const uint8_t back[2] = {0xEB, 0xF9};  // jmp short -7 (to the long jump)

        return Write(f - 5, j, sizeof(j)) && Write(f, back, sizeof(back)) ? f + 2 : nullptr;
    }

    static const uint8_t kTeb[6] = {0x64, 0xA1, 0x18, 0x00, 0x00, 0x00};

    if (std::memcmp(f, kTeb, sizeof(kTeb)) == 0) {
        auto t = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!t) {
            return nullptr;
        }

        std::memcpy(t, kTeb, sizeof(kTeb));
        t[6] = 0xE9;  // jmp f+6
        const int32_t back = int32_t(reinterpret_cast<uintptr_t>(f + 6) - reinterpret_cast<uintptr_t>(t + 11));

        std::memcpy(t + 7, &back, 4);
        uint8_t j[6] = {0xE9, 0, 0, 0, 0, 0x90};
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(f + 5));

        std::memcpy(j + 1, &rel, 4);
        return Write(f, j, sizeof(j)) ? t : nullptr;
    }

    return nullptr;
}

template <class T>
bool Hook(HMODULE m, const char* name, T hook, T* orig) {
    *orig = reinterpret_cast<T>(HookExport(m, name, reinterpret_cast<void*>(hook)));

    if (!*orig) {
        const auto f = reinterpret_cast<const uint8_t*>(GetProcAddress(m, name));

        Rs7Log("window: cannot hook user32!%s (first bytes %02X %02X) - window setting partly off", name, f ? f[0] : 0,
               f ? f[1] : 0);
        return false;
    }

    return true;
}
}  // namespace

bool Rs7Window_Background() {
    return g_background;
}

void Rs7Window_Install(const wchar_t* iniPath) {
    g_background = GetPrivateProfileIntW(L"window", L"background", 0, iniPath) != 0;
    g_screen = static_cast<int>(GetPrivateProfileIntW(L"window", L"screen", 0, iniPath));
    if (g_screen < 0) {
        g_screen = 0;
    }
    Rs7Log("config: [window] background=%d screen=%d%s", g_background ? 1 : 0, g_screen,
           g_background || g_screen ? "" : " -> stock window, no hooks");
    if (!g_background && !g_screen) {
        return;
    }

    const HMODULE u = GetModuleHandleW(L"user32.dll");  // no LoadLibrary under the loader lock

    if (!u) {
        Rs7Log("window: user32.dll not loaded yet - window setting off");
        return;
    }

    // Hook the callers before CreateWindowExA, which marks the game window.
    bool ok = Hook(u, "ShowWindow", &ShowWindowHook, &g_show) & Hook(u, "MoveWindow", &MoveWindowHook, &g_move) &
              Hook(u, "SetWindowPos", &SetWindowPosHook, &g_setPos);

    if (g_background) {
        const auto game = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(game + dos->e_lfanew);

        g_gameBegin = reinterpret_cast<uintptr_t>(game);
        g_gameEnd = g_gameBegin + nt->OptionalHeader.SizeOfImage;
        ok &= Hook(u, "SetForegroundWindow", &SetForegroundWindowHook, &g_setFg) &
              Hook(u, "BringWindowToTop", &BringWindowToTopHook, &g_toTop) &
              Hook(u, "SetActiveWindow", &SetActiveWindowHook, &g_setActive) &
              Hook(u, "SetFocus", &SetFocusHook, &g_setFocus) & Hook(u, "GetFocus", &GetFocusHook, &g_getFocus) &
              Hook(u, "GetForegroundWindow", &GetForegroundWindowHook, &g_getForeground);
    }

    // Without the callers above there is nothing to guard, so leave the window unmarked.
    if (ok) {
        ok = Hook(u, "CreateWindowExA", &CreateWindowExAHook, &g_create);
    }
    Rs7Log("window: user32 hooks %s", ok ? "installed" : "INCOMPLETE (game window left stock)");
}
