#pragma once
#include <windows.h>
#include <d3d9.h>

// The overlay's frame hook (overlay/overlay.h): slots 17 (Present) and 16 (Reset) of the game
// device's vtable, the same technique as d3d/swap_hook.cpp (hot-patchable thunks, COM vtables only, no
// d3d9.lib). The ramp worker hands the device over (RsxrRamp_RunWorker's device callback).
//
// Why Present and not EndScene: RSMods (xinput1_3.dll) inline-hooks the d3d9 EndScene body and
// skips its own work for every EndScene whose return address lies outside the game exe
// (Menu::IsOverlayCall). A vtable EndScene hook would call that body from our DLL, so RSMods would
// skip its menu and overlay for the game's own EndScene. RSMods does not hook Present; our draw runs
// in its own BeginScene/EndScene pair right before the game's Present (RSMods sees that EndScene as
// an overlay's and leaves it alone), once per frame.
//
// Callbacks run on the render thread, inside an SEH guard: a fault in one of them is counted and
// the callback is not called again (sticky), the game's call always goes on to the original.
struct RsxrPresentCallbacks {
    void (*onPresent)(IDirect3DDevice9* dev);               // before the original Present
    void (*beforeReset)(IDirect3DDevice9* dev);             // before the original Reset
    void (*afterReset)(IDirect3DDevice9* dev, HRESULT hr);  // after it
    void (*onFault)() = nullptr;  // release input without calling ImGui; first callback fault only
};

// Patches slots 16 and 17 of `device`'s vtable (Reset first: once Present can draw, Reset already
// releases). Idempotent; a second device is refused. Logs the slot values and their owning module.
bool RsxrPresent_InstallOn(void* device, const RsxrPresentCallbacks& cb);
// Worker, every round once hooked: re-hook if d3d9 restored its entry points; leave foreign hooks
// alone (logged once).
void RsxrPresent_Recheck();
// True once Present/Reset are hooked (worker: retry the overlay install while false).
bool RsxrPresent_Installed();
// Calls of the hooked Present / faults in the callbacks so far.
unsigned long RsxrPresent_Frames();
unsigned long RsxrPresent_Faults();

extern "C" void Rsxr_Thunk_Present();
extern "C" void Rsxr_Thunk_Reset();

// Tests: restores both slots and forgets the device and the callbacks.
void RsxrPresent_UninstallForTests();
