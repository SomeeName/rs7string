// The SetTexture / GetTexture vtable hook of the ramp swap: patches slots 64/65 of the game device's vtable
// with hot-patchable thunks, keeps them in place (Recheck), and runs the render-thread work of swap_ramp.cpp
// inside an SEH guard. GetTexture hides the swap from whoever asks.
#include "d3d/swap_hook.h"
#include "d3d/hookutil.h"
#include "d3d/swap.h"
#include "d3d/swap_ramp.h"
#include "features/colours/colours.h"
#include "platform/patch.h"
#include <cstdio>
#include <cstring>

extern "C" HRESULT __stdcall Rs7_SetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
extern "C" HRESULT __stdcall Rs7_GetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out);

// Hot-patchable thunks: the first 5 bytes are relocatable whole instructions, so a trampoline hooker
// (RSMods' MemUtil::TrampHook reads vTable[65] from its dummy device) can hook this slot after us.
extern "C" __declspec(naked) void Rs7_Thunk_SetTexture() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_SetTextureHook
    }
}

extern "C" __declspec(naked) void Rs7_Thunk_GetTexture() {
    __asm {
        mov edi, edi
        push ebp
        mov ebp, esp
        pop ebp
        jmp Rs7_GetTextureHook
    }
}

namespace rs7swap {
HookState g_hook;
}  // namespace rs7swap

namespace {
using rs7d3d::LogQuiet;
using rs7swap::g_hook;
using rs7swap::kStages;
constexpr unsigned kSlotGet = 64, kSlotSet = 65;

// Describes where a d3d9 entry point really goes: "d3d9.dll", or "inline-hooked -> <module>".
uintptr_t JumpTarget(uintptr_t fn) {
    uint8_t b[6] = {};
    if (!g_hook.mem.Read(fn, b, sizeof b)) {
        return 0;
    }

    if (b[0] == 0xE9) {
        int32_t r;

        std::memcpy(&r, b + 1, 4);
        return fn + 5 + r;
    }

    if (b[0] == 0xEB) {
        return fn + 2 + int8_t(b[1]);
    }

    if (b[0] == 0xFF && b[1] == 0x25) {
        uint32_t slot, dest = 0;

        std::memcpy(&slot, b + 2, 4);
        return g_hook.mem.Read(slot, &dest, 4) ? dest : 0;
    }

    return 0;
}

void DescribeBody(uintptr_t fn, char* out, size_t n) {
    char mod[MAX_PATH];
    const uintptr_t to = JumpTarget(fn);

    rs7d3d::ModuleName(reinterpret_cast<const void*>(to ? to : fn), mod, sizeof mod);
    if (to) {
        std::snprintf(out, n, "inline-hooked -> %s", mod);
    } else {
        std::snprintf(out, n, "%s", mod);
    }
}
}  // namespace

extern "C" HRESULT __stdcall Rs7_SetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
    IDirect3DBaseTexture9* bind = tex;

    __try {
        bind = rs7swap::OnSet(dev, stage, tex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        bind = tex;
        rs7swap::AfterFault();
    }

    return g_hook.origSet(dev, stage, bind);
}

// Hide the swap: whoever asks (RSMods' CaptureNoteColorRamp / stem CRCs) gets the stock texture.
extern "C" HRESULT __stdcall Rs7_GetTextureHook(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out) {
    const HRESULT hr = g_hook.origGet(dev, stage, out);

    __try {
        if (SUCCEEDED(hr) && out && *out && dev == g_hook.dev && stage < kStages && g_hook.bound[stage].ours == *out) {
            (*out)->Release();  // cannot free: the candidate holds a reference
            *out = g_hook.bound[stage].stock;
            (*out)->AddRef();
            Rs7Ramp_CountHidden();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Rs7Ramp_CountFault();
    }

    return hr;
}

bool Rs7Swap_Installed() {
    return g_hook.installed;
}

bool Rs7Swap_InstallOn(void* device) {
    if (!device) {
        return false;
    }

    void** vt = *static_cast<void***>(device);
    void* const thSet = reinterpret_cast<void*>(&Rs7_Thunk_SetTexture);
    void* const thGet = reinterpret_cast<void*>(&Rs7_Thunk_GetTexture);
    if (vt[kSlotSet] == thSet && vt[kSlotGet] == thGet) {
        return true;  // idempotent
    }

    if (g_hook.installed) {
        LogQuiet("ramp: device %p has another vtable %p (hooked %p): not hooked", device, static_cast<void*>(vt),
                 static_cast<void*>(g_hook.vt));
        return false;
    }

    char bodySet[MAX_PATH + 32], bodyGet[MAX_PATH + 32];

    DescribeBody(reinterpret_cast<uintptr_t>(vt[kSlotSet]), bodySet, sizeof bodySet);
    DescribeBody(reinterpret_cast<uintptr_t>(vt[kSlotGet]), bodyGet, sizeof bodyGet);
    g_hook.origGet = reinterpret_cast<rs7swap::GetTex>(vt[kSlotGet]);
    g_hook.origSet = reinterpret_cast<rs7swap::SetTex>(vt[kSlotSet]);
    g_hook.dev = static_cast<IDirect3DDevice9*>(device);
    g_hook.vt = vt;
    g_hook.deviceOk = true;
    uintptr_t old = 0;

    // GetTexture first: once SetTexture can swap, GetTexture already hides it.
    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotGet]), reinterpret_cast<uintptr_t>(thGet), &old)) {
        LogQuiet("ramp: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotGet);
        g_hook.dev = nullptr;
        g_hook.vt = nullptr;
        return false;
    }

    if (!Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotSet]), reinterpret_cast<uintptr_t>(thSet), &old)) {
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&vt[kSlotGet]), reinterpret_cast<uintptr_t>(g_hook.origGet),
                           &old);
        LogQuiet("ramp: device %p vtable %p: cannot write slot %u - not hooked", device, static_cast<void*>(vt),
                 kSlotSet);
        g_hook.dev = nullptr;
        g_hook.vt = nullptr;
        return false;
    }

    g_hook.installed = true;
    LogQuiet("ramp: device %p vtable %p: SetTexture slot %p (body %s) GetTexture slot %p (body %s) -> hooked; "
             "RSMods (xinput1_3.dll) %s",
             device, static_cast<void*>(vt), reinterpret_cast<void*>(g_hook.origSet), bodySet,
             reinterpret_cast<void*>(g_hook.origGet), bodyGet,
             GetModuleHandleW(L"xinput1_3.dll") ? "present" : "absent");

    return true;
}

void Rs7Swap_Recheck() {
    static bool foreignLogged = false;
    if (!g_hook.installed || !g_hook.vt) {
        return;
    }

    uintptr_t slots[2] = {};
    if (!g_hook.mem.Read(reinterpret_cast<uintptr_t>(&g_hook.vt[kSlotGet]), slots, sizeof slots)) {
        return;
    }

    const uintptr_t thGet = reinterpret_cast<uintptr_t>(&Rs7_Thunk_GetTexture);
    const uintptr_t thSet = reinterpret_cast<uintptr_t>(&Rs7_Thunk_SetTexture);
    if (slots[0] == thGet && slots[1] == thSet) {
        return;
    }

    if (slots[0] == reinterpret_cast<uintptr_t>(g_hook.origGet) &&
        slots[1] == reinterpret_cast<uintptr_t>(g_hook.origSet)) {
        uintptr_t old = 0;
        const bool ok = Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_hook.vt[kSlotGet]), thGet, &old) &&
                        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_hook.vt[kSlotSet]), thSet, &old);

        LogQuiet("ramp: device %p: slots were reset to d3d9's entry points - %s", static_cast<void*>(g_hook.dev),
                 ok ? "hooked again" : "re-hook FAILED");
        return;
    }

    if (!foreignLogged) {
        foreignLogged = true;
        LogQuiet("ramp: device %p: slot 64/65 now %08X/%08X (not ours, not d3d9's): left alone",
                 static_cast<void*>(g_hook.dev), unsigned(slots[0]), unsigned(slots[1]));
    }
}

void Rs7Swap_UninstallForTests() {
    if (g_hook.vt) {
        uintptr_t old = 0;

        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_hook.vt[kSlotSet]),
                           reinterpret_cast<uintptr_t>(g_hook.origSet), &old);
        Rs7_WritePtrAtomic(reinterpret_cast<uintptr_t>(&g_hook.vt[kSlotGet]),
                           reinterpret_cast<uintptr_t>(g_hook.origGet), &old);
    }

    rs7swap::ResetRamp();

    for (auto& b : g_hook.bound) {
        b = {};
    }

    g_hook.vt = nullptr;
    g_hook.dev = nullptr;
    g_hook.origSet = nullptr;
    g_hook.origGet = nullptr;
    g_hook.installed = false;
    g_hook.deviceOk = true;
}

HRESULT Rs7Swap_RawGetTextureForTests(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9** out) {
    return g_hook.origGet ? g_hook.origGet(dev, stage, out) : dev->GetTexture(stage, out);
}
