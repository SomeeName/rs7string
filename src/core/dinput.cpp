#include "core/dinput.h"
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#define DIRECTINPUT_VERSION 0x0800
#include "core/overlaycmd.h"
#include "core/protect.h"
#include "core/window.h"
#include "log.h"
#include <dinput.h>

DWORD Rs7Dinput_CoopFlags(DWORD devType, DWORD flags, bool background) {
    const DWORD type = GET_DIDEVICE_TYPE(devType);
    if (!background || (type != DI8DEVTYPE_KEYBOARD && type != DI8DEVTYPE_MOUSE)) {
        return flags;
    }

    return (flags & ~DWORD(DISCL_BACKGROUND)) | DISCL_FOREGROUND;
}

bool Rs7Dinput_ParseKeyLine(const char* line, uint8_t* dik, bool* down) {
    if (!line || *line != 'k') {
        return false;
    }

    const char* p = line + 1;
    auto number = [&](int base, unsigned long max, unsigned long* value) {
        while (std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }

        if (*p == '-') {
            return false;
        }

        char* end = nullptr;

        errno = 0;
        const unsigned long n = std::strtoul(p, &end, base);
        if (end == p || errno == ERANGE || n > max) {
            return false;
        }

        p = end;
        *value = n;

        return true;
    };
    unsigned long code = 0, d = 0;
    if (!number(16, 0xFF, &code) || code == 0 || !number(10, 1, &d)) {
        return false;
    }

    while (std::isspace(static_cast<unsigned char>(*p))) {
        ++p;
    }

    if (*p) {
        return false;
    }

    *dik = uint8_t(code);
    *down = d != 0;

    return true;
}

namespace {
// vtable slots: IDirectInput8 3 = CreateDevice; IDirectInputDevice8 3 = GetCapabilities,
// 7 = Acquire, 9 = GetDeviceState, 10 = GetDeviceData, 13 = SetCooperativeLevel (same layout
// for the A and W interfaces; one vtable is shared by all devices of an interface).
constexpr int kCreateDevice = 3, kGetCaps = 3, kAcquire = 7, kGetState = 9, kGetData = 10, kSetCoop = 13;

using CreateDevice_t = HRESULT(WINAPI*)(void*, REFGUID, void**, LPUNKNOWN);
using GetCaps_t = HRESULT(WINAPI*)(void*, DIDEVCAPS*);
using SetCoop_t = HRESULT(WINAPI*)(void*, HWND, DWORD);
using Acquire_t = HRESULT(WINAPI*)(void*);
using GetState_t = HRESULT(WINAPI*)(void*, DWORD, void*);
using GetData_t = HRESULT(WINAPI*)(void*, DWORD, DIDEVICEOBJECTDATA*, DWORD*, DWORD);

// One entry per patched vtable (the A and W interfaces have their own).
struct Slot {
    void** vt;
    void* orig;
};

Slot g_create[2], g_coop[2], g_acquire[2], g_state[2], g_data[2];
volatile LONG g_logBudget = 16;

// Injected keyboard (background test driving): fed by the pipe thread, read by the game thread.
void* volatile g_kbd;  // the game's keyboard device
CRITICAL_SECTION g_lock;
uint8_t g_keys[256];                       // current injected key state (0x80 = down)
std::deque<DIDEVICEOBJECTDATA>* g_events;  // buffered events not yet read by GetDeviceData
DWORD g_seq;
volatile LONG g_injectLogged;

void* OrigFor(Slot* slots, void** vt) {
    for (int i = 0; i < 2; ++i) {
        if (slots[i].vt == vt) {
            return slots[i].orig;
        }
    }

    return nullptr;
}

// Replaces vt[index] with `hook` once per vtable; false when the slot table is full or the write fails.
bool PatchSlot(Slot* slots, void** vt, int index, void* hook) {
    for (int i = 0; i < 2; ++i) {
        if (slots[i].vt == vt) {
            return true;
        }

        if (!slots[i].vt) {
            DWORD old = 0;
            if (!Rs7_Protect(&vt[index], sizeof(void*), PAGE_READWRITE, &old)) {
                return false;
            }

            slots[i].orig = vt[index];
            slots[i].vt = vt;
            vt[index] = hook;
            Rs7_Protect(&vt[index], sizeof(void*), old, &old);
            return true;
        }
    }

    return false;
}

void** Vt(void* obj) {
    return *static_cast<void***>(obj);
}

bool Injecting(void* dev) {
    return dev && dev == g_kbd && Rs7Window_Background();
}

void Push(uint8_t dik, bool down) {
    EnterCriticalSection(&g_lock);
    g_keys[dik] = down ? 0x80 : 0;
    DIDEVICEOBJECTDATA e{};

    e.dwOfs = dik;
    e.dwData = down ? 0x80 : 0;
    e.dwTimeStamp = GetTickCount();
    e.dwSequence = ++g_seq;
    if (g_events->size() < 256) {
        g_events->push_back(e);
    }
    LeaveCriticalSection(&g_lock);
}

// \\.\pipe\rs7string: lines "k <DIK hex> <1 down|0 up>" (scripts/rsdrive.py) and overlay
// commands "o ..." (core/overlaycmd.h, tools/rsoverlay.py). Same-user only
// (default pipe security). Only started in background mode.
DWORD WINAPI PipeThread(LPVOID) {
    for (;;) {
        const HANDLE p = CreateNamedPipeW(L"\\\\.\\pipe\\rs7string", PIPE_ACCESS_INBOUND,
                                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                          1, 0, 4096, 0, nullptr);

        if (p == INVALID_HANDLE_VALUE) {
            Rs7Log("dinput: cannot create the key pipe (%lu) - no injected keys", GetLastError());
            return 0;
        }

        if (ConnectNamedPipe(p, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buf[512], line[400];
            size_t n = 0;
            DWORD got = 0;

            while (ReadFile(p, buf, sizeof(buf), &got, nullptr) && got) {
                for (DWORD i = 0; i < got; ++i) {
                    if (buf[i] == '\n' || n + 1 >= sizeof(line)) {
                        line[n] = 0;
                        n = 0;
                        uint8_t dik;
                        bool down;

                        if (Rs7Dinput_ParseKeyLine(line, &dik, &down)) {
                            Push(dik, down);
                        } else if (line[0] == 'o' && line[1] == ' ') {
                            Rs7Cmd_Post(line);  // overlay (overlaycmd.h)
                        }
                    } else if (buf[i] != '\r') {
                        line[n++] = buf[i];
                    }
                }
            }
        }

        DisconnectNamedPipe(p);
        CloseHandle(p);
    }
}

HRESULT WINAPI AcquireHook(void* dev) {
    const auto orig = reinterpret_cast<Acquire_t>(OrigFor(g_acquire, Vt(dev)));
    if (!orig) {
        return DIERR_GENERIC;
    }

    const HRESULT hr = orig(dev);

    // Not the foreground window: the real keyboard stays unacquired, the injected one is "acquired".
    return FAILED(hr) && Injecting(dev) ? DI_OK : hr;
}

HRESULT WINAPI GetStateHook(void* dev, DWORD cb, void* data) {
    const auto orig = reinterpret_cast<GetState_t>(OrigFor(g_state, Vt(dev)));
    if (!orig) {
        return DIERR_GENERIC;
    }

    HRESULT hr = orig(dev, cb, data);
    if (!Injecting(dev) || !data) {
        return hr;
    }

    if (InterlockedExchange(&g_injectLogged, 1) == 0) {
        Rs7Log("dinput: the game reads the keyboard with GetDeviceState");
    }

    if (FAILED(hr)) {
        std::memset(data, 0, cb);
        hr = DI_OK;
    }

    EnterCriticalSection(&g_lock);

    for (DWORD i = 0; i < cb && i < 256; ++i) {
        static_cast<uint8_t*>(data)[i] |= g_keys[i];
    }

    LeaveCriticalSection(&g_lock);

    return hr;
}

HRESULT WINAPI GetDataHook(void* dev, DWORD cbObj, DIDEVICEOBJECTDATA* out, DWORD* inOut, DWORD flags) {
    const auto orig = reinterpret_cast<GetData_t>(OrigFor(g_data, Vt(dev)));
    if (!orig) {
        return DIERR_GENERIC;
    }
    if (!Injecting(dev) || !inOut || (cbObj != sizeof(DIDEVICEOBJECTDATA) && out)) {
        return orig(dev, cbObj, out, inOut, flags);
    }

    if (InterlockedExchange(&g_injectLogged, 2) == 0) {
        Rs7Log("dinput: the game reads the keyboard with GetDeviceData");
    }
    const DWORD cap = *inOut;
    HRESULT hr = orig(dev, cbObj, out, inOut, flags);
    DWORD used = SUCCEEDED(hr) ? *inOut : 0;

    hr = DI_OK;
    EnterCriticalSection(&g_lock);
    const bool peek = (flags & DIGDD_PEEK) != 0;
    size_t i = 0;

    for (; i < g_events->size() && (!out || used < cap); ++i, ++used) {
        if (out) {
            out[used] = (*g_events)[i];
        }
    }

    if (!peek) {
        g_events->erase(g_events->begin(), g_events->begin() + std::ptrdiff_t(i));
    }
    LeaveCriticalSection(&g_lock);
    *inOut = used;

    return hr;
}

HRESULT WINAPI SetCoopHook(void* dev, HWND hwnd, DWORD flags) {
    void** vt = Vt(dev);
    const auto orig = reinterpret_cast<SetCoop_t>(OrigFor(g_coop, vt));
    if (!orig) {
        return DIERR_GENERIC;
    }

    DIDEVCAPS caps{};

    caps.dwSize = sizeof(caps);
    const bool known = SUCCEEDED(reinterpret_cast<GetCaps_t>(vt[kGetCaps])(dev, &caps));
    const DWORD out = known ? Rs7Dinput_CoopFlags(caps.dwDevType, flags, Rs7Window_Background()) : flags;
    const HRESULT hr = orig(dev, hwnd, out);

    if (InterlockedDecrement(&g_logBudget) >= 0) {
        Rs7Log("dinput: device type %02X SetCooperativeLevel(%p, %02X%s) -> %08X",
               unsigned(GET_DIDEVICE_TYPE(caps.dwDevType)), static_cast<void*>(hwnd), unsigned(out),
               out != flags ? " (background test driving: foreground only)" : "", unsigned(hr));
    }

    if (known && GET_DIDEVICE_TYPE(caps.dwDevType) == DI8DEVTYPE_KEYBOARD && Rs7Window_Background() && !g_kbd) {
        const bool ok = PatchSlot(g_acquire, vt, kAcquire, reinterpret_cast<void*>(&AcquireHook)) &&
                        PatchSlot(g_state, vt, kGetState, reinterpret_cast<void*>(&GetStateHook)) &&
                        PatchSlot(g_data, vt, kGetData, reinterpret_cast<void*>(&GetDataHook));

        if (ok) {
            InitializeCriticalSection(&g_lock);
            g_events = new std::deque<DIDEVICEOBJECTDATA>();
            g_kbd = dev;
            const HANDLE t = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr);

            if (t) {
                CloseHandle(t);
            }
        }

        Rs7Log("dinput: keyboard %p: injected keys via \\\\.\\pipe\\rs7string %s", dev,
               ok ? "ON" : "OFF (hook failed)");
    }

    return hr;
}

HRESULT WINAPI CreateDeviceHook(void* di, REFGUID guid, void** out, LPUNKNOWN outer) {
    const auto orig = reinterpret_cast<CreateDevice_t>(OrigFor(g_create, Vt(di)));
    if (!orig) {
        return DIERR_GENERIC;
    }

    const HRESULT hr = orig(di, guid, out, outer);

    if (SUCCEEDED(hr) && out && *out && !PatchSlot(g_coop, Vt(*out), kSetCoop, reinterpret_cast<void*>(&SetCoopHook))) {
        Rs7Log("dinput: cannot hook SetCooperativeLevel - cooperative levels left stock");
    }

    return hr;
}
}  // namespace

void Rs7Dinput_Wrap(void* directInput8) {
    if (!directInput8) {
        return;
    }

    if (!PatchSlot(g_create, Vt(directInput8), kCreateDevice, reinterpret_cast<void*>(&CreateDeviceHook))) {
        Rs7Log("dinput: cannot hook CreateDevice - cooperative levels left stock");
    }
}
