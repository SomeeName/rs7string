#include "d3d/present.h"
#include "d3d/hookutil.h"
#include "d3d/sehread.h"
#include "d3d/swap.h"
#include "platform/seh.h"
#include <cstdint>

extern "C" HRESULT __stdcall Rs7_PresentHook(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                             const RGNDATA* dirty);
extern "C" HRESULT __stdcall Rs7_ResetHook(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp);

// Hot-patchable thunks, as in d3d/swap_hook.cpp: a trampoline hooker reading these slots can hook them after us.
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
const rs7d3d::VtSlotPair kSlots{
    "overlay",
    kSlotReset,
    {reinterpret_cast<uintptr_t>(&Rs7_Thunk_Reset), reinterpret_cast<uintptr_t>(&Rs7_Thunk_Present)}};
static_assert(kSlotPresent == kSlotReset + 1, "VtSlotPair hooks two neighbouring slots");

// d3d9's entry points of slots 16/17.
rs7d3d::SlotValues Orig() {
    return {reinterpret_cast<uintptr_t>(g_origReset), reinterpret_cast<uintptr_t>(g_origPresent)};
}

void Fault(const char* where) {
    InterlockedIncrement(&g_faults);
    const bool first = !g_dead;

    g_dead = true;

    if (first && g_cb.onFault) {
        __try {
            g_cb.onFault();
        } __except (Rs7_FaultFilter(GetExceptionCode())) {
            LogQuiet("overlay: fault while disabling input capture");
        }
    }

    LogQuiet("overlay: fault in the %s callback - overlay drawing OFF for this run", where);
}

void CallPresent(IDirect3DDevice9* dev) {
    __try {
        __try {
            Rs7Swap_SetOverlayDrawing(dev, true);
            g_cb.onPresent(dev);
        } __finally {
            Rs7Swap_SetOverlayDrawing(dev, false);
        }
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        Fault("Present");
    }
}

void CallBeforeReset(IDirect3DDevice9* dev) {
    __try {
        g_cb.beforeReset(dev);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        Fault("before-Reset");
    }
}

void CallAfterReset(IDirect3DDevice9* dev, HRESULT hr) {
    __try {
        g_cb.afterReset(dev, hr);
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
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
    if (kSlots.Ours(vt)) {
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

    if (!kSlots.Write(device, vt, Orig())) {
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

    kSlots.Recheck(g_dev, g_vt, Orig(), g_mem, &foreignLogged);
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
        kSlots.Restore(g_vt, Orig());
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
