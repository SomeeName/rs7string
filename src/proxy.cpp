// dinput8.dll proxy: every export forwards to the system dinput8.dll (SysWOW64 for this
// 32-bit process). The game only imports DirectInput8Create; the rest exist for completeness.
#include "core/dinput.h"
#include <windows.h>
#include <unknwn.h>

namespace {
HMODULE RealDinput8() {
    static HMODULE h = [] {
        wchar_t path[MAX_PATH];
        const UINT n = GetSystemDirectoryW(path, MAX_PATH);

        lstrcpyW(path + n, L"\\dinput8.dll");

        return LoadLibraryW(path);
    }();

    return h;
}

template <class Fn>
Fn Real(const char* name) {
    const HMODULE h = RealDinput8();

    return h ? reinterpret_cast<Fn>(GetProcAddress(h, name)) : nullptr;
}
}  // namespace

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out,
                                                   LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const Fn f = Real<Fn>("DirectInput8Create");
    const HRESULT hr = f ? f(inst, ver, riid, out, outer) : E_FAIL;

    if (SUCCEEDED(hr) && out && !outer) {
        Rs7Dinput_Wrap(*out);
    }

    return hr;
}

extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    const Fn f = Real<Fn>("DllCanUnloadNow");

    return f ? f() : S_FALSE;
}

extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    const Fn f = Real<Fn>("DllGetClassObject");

    return f ? f(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI Proxy_DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const Fn f = Real<Fn>("DllRegisterServer");

    return f ? f() : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    const Fn f = Real<Fn>("DllUnregisterServer");

    return f ? f() : E_FAIL;
}

extern "C" LPCVOID WINAPI Proxy_GetdfDIJoystick() {
    using Fn = LPCVOID(WINAPI*)();
    const Fn f = Real<Fn>("GetdfDIJoystick");

    return f ? f() : nullptr;
}
