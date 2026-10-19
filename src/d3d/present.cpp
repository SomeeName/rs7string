#include "d3d/present.h"
#include "d3d/hookutil.h"
#include "d3d/sehread.h"
#include "platform/patch.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" HRESULT __stdcall Rs7_PresentHook(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                             const RGNDATA* dirty);
extern "C" HRESULT __stdcall Rs7_ResetHook(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp);

// Hot-patchable thunks, as d3d/swap_hook.cpp: a trampoline hooker reading these slots can hook them after us.
extern "C" __declspec(naked) void Rs7_Thunk_Present() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_PresentHook
    }
}

extern "C" __declspec(naked) void Rs7_Thunk_Reset() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_ResetHook
    }
}

namespace {
using rs7d3d::LogQuiet;
using rs7d3d::ModuleName;
constexpr unsigned kSlotReset = 16, kSlotPresent = 17;
using Present_t = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

Present_t g_origPresent;
Reset_t g_origReset;
IDirect3DDevice9* g_dev;
void** g_vt;
volatile bool g_installed;
volatile bool g_dead;  // a callback faulted: no callback any more
Rs7PresentCallbacks g_cb;
volatile LONG g_frames, g_faults;
const Rs7SehReader g_mem{};

void Fault(const char* where) {
    InterlockedIncrement(&g_faults);
    g_dead = true;
    LogQuiet("overlay: fault in the %s callback - overlay drawing OFF for this run", where);
}

void CallPresent(IDirect3DDevice9* dev) {
    __try {
        g_cb.onPresent(dev);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault("Present");
    }
}

void CallBeforeReset(IDirect3DDevice9* dev) {
    __try {
        g_cb.beforeReset(dev);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault("before-Reset");
    }
}

void CallAfterReset(IDirect3DDevice9* dev, HRESULT hr) {
    __try {
        g_cb.afterReset(dev, hr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault("after-Reset");
    }
}
}  // namespace

extern "C" HRESULT __stdcall Rs7_PresentHook(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                             const RGNDATA* dirty) {
    InterlockedIncrement(&g_frames);
    if (dev == g_dev && !g_dead && g_cb.onPresent) {
        CallPresent(dev);
    }

    return g_origPresent(dev, src, dst, wnd, dirty);
}

extern "C" HRESULT __stdcall Rs7_ResetHook(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    // beforeReset must run even after a fault elsewhere: our DEFAULT-pool objects would make the game's
    // Reset fail. Its own fault only stops the callbacks.
    const bool ours = dev == g_dev;

    if (ours && g_cb.beforeReset) {
        CallBeforeReset(dev);
    }
    const HRESULT hr = g_origReset(dev, pp);

    if (ours && !g_dead && g_cb.afterReset) {
        CallAfterReset(dev, hr);
    }

    return hr;
}

bool Rs7Present_InstallOn(void* device, const Rs7PresentCallbacks& cb) {
    if (!device) {
        return false;
    }

    void** vt = *static_cast<void***>(device);
    void* const thPresent = reinterpret_cast<void*>(&Rs7_Thunk_Present);
    void* const thReset = reinterpret_cast<void*>(&Rs7_Thunk_Reset);
    if (vt[kSlotPresent] == thPresent && vt[kSlotReset] == thReset) {
        return true;  // idempotent
    }

    if (g_installed) {
        LogQuiet("overlay: device %p has another vtable %p (hooked %p): not hooked", device, static_cast<void*>(vt),
                 static_cast<void*>(g_vt));
        return false;
    }

    char modPresent[MAX_PATH], modReset[MAX_PATH];

    ModuleName(vt[kSlotPresent], modPresent, sizeof modPresent);
    ModuleName(vt[kSlotReset], modReset, sizeof modReset);
    g_origPresent = reinterpret_cast<Present_t>(vt[kSlotPresent]);
    g_origReset = reinterpret_cast<Reset_t>(vt[kSlotReset]);
    g_cb = cb;
    g_dev = static_cast<IDirect3DDevice9*>(device);
    g_vt = vt;
    g_dead = false;
    uintptr_t old = 0;

    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotReset]), reinterpret_cast<uintptr_t>(thReset), &old)) {
        LogQuiet("overlay: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotReset);
        g_dev = nullptr;
        g_vt = nullptr;
        return false;
    }

    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotPresent]), reinterpret_cast<uintptr_t>(thPresent),
                            &old)) {
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotReset]), reinterpret_cast<uintptr_t>(g_origReset),
                           &old);
        LogQuiet("overlay: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotPresent);
        g_dev = nullptr;
        g_vt = nullptr;
        return false;
    }

    g_installed = true;
    LogQuiet("overlay: device %p vtable %p: Present slot %p (%s) Reset slot %p (%s) -> hooked", device,
             static_cast<void*>(vt), reinterpret_cast<void*>(g_origPresent), modPresent,
             reinterpret_cast<void*>(g_origReset), modReset);

    return true;
}

void Rs7Present_Recheck() {
    static bool foreignLogged = false;
    if (!g_installed || !g_vt) {
        return;
    }

    uintptr_t slots[2] = {};  // [0] = Reset (16), [1] = Present (17)
    if (!g_mem.Read(reinterpret_cast<uintptr_t>(&g_vt[kSlotReset]), slots, sizeof slots)) {
        return;
    }

    const uintptr_t thReset = reinterpret_cast<uintptr_t>(&Rs7_Thunk_Reset);
    const uintptr_t thPresent = reinterpret_cast<uintptr_t>(&Rs7_Thunk_Present);
    if (slots[0] == thReset && slots[1] == thPresent) {
        return;
    }

    if (slots[0] == reinterpret_cast<uintptr_t>(g_origReset) &&
        slots[1] == reinterpret_cast<uintptr_t>(g_origPresent)) {
        uintptr_t old = 0;
        const bool ok = Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotReset]), thReset, &old) &&
                        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotPresent]), thPresent, &old);

        LogQuiet("overlay: device %p: slots 16/17 were reset to d3d9's entry points - %s", static_cast<void*>(g_dev),
                 ok ? "hooked again" : "re-hook FAILED");
        return;
    }

    if (!foreignLogged) {
        foreignLogged = true;
        LogQuiet("overlay: device %p: slot 16/17 now %08X/%08X (not ours, not d3d9's): left alone",
                 static_cast<void*>(g_dev), unsigned(slots[0]), unsigned(slots[1]));
    }
}

bool Rs7Present_Installed() {
    return g_installed;
}

unsigned long Rs7Present_Frames() {
    return static_cast<unsigned long>(g_frames);
}

unsigned long Rs7Present_Faults() {
    return static_cast<unsigned long>(g_faults);
}

void Rs7Present_UninstallForTests() {
    if (g_vt) {
        uintptr_t old = 0;

        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotPresent]), reinterpret_cast<uintptr_t>(g_origPresent),
                           &old);
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_vt[kSlotReset]), reinterpret_cast<uintptr_t>(g_origReset),
                           &old);
    }

    g_vt = nullptr;
    g_dev = nullptr;
    g_origPresent = nullptr;
    g_origReset = nullptr;
    g_cb = {};
    g_installed = false;
    g_dead = false;
    g_frames = 0;
    g_faults = 0;
}
