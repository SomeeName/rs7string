#pragma once
#include "features/colours/colours.h"
#include <windows.h>
#include <cstdint>
#include <d3d9.h>

// The game device's vtable slots 64 (GetTexture) and 65 (SetTexture) point at
// our thunks. On the render thread, the first time the game binds a candidate (a stock ramp found by
// the worker), Process copies it into a same-size MANAGED texture with the string-6 band and, after the
// self-check, binds the copy in its place; GetTexture returns the stock again. Any refusal or fault:
// pass-through and V6 off. Uses only COM vtables (no d3d9.lib in the DLL).
//
// Parts (each file one job; this header is the public interface of all of them):
//   swap_hook.cpp     the vtable hook: thunks, install / recheck, GetTexture hiding
//   swap_ramp.cpp     the stock ramps: candidate processing, recolour, retire, overlay status
//   swap_foreign.cpp  RSMods' own note ramps
//   swap_texio.cpp    texture level reads, copy build and write-back with read-back check

struct RsxrSwapCfg {
    uint32_t rgb[2];  // rgb[mode] = 0xRRGGBB
    // RSMods is installed: its own note ramps (custom string colours, Extended Range) bound on stage 1 get
    // a copy with string 6's lines too (same build + self-check; see swap_foreign.cpp).
    bool rsmods = false;
};

void RsxrSwap_Configure(const RsxrSwapCfg&);
// The game's NiTexture list lock and d3d9.dll's image range, for the render thread's revalidation
// (RsxrTex_Revalidate) before it takes a reference on a candidate. Set by the worker.
void RsxrSwap_SetEngine(CRITICAL_SECTION* listLock, uintptr_t d3d9Lo, uintptr_t d3d9Hi);
// Patches slots 64 (GetTexture) and 65 (SetTexture) of the device's vtable. Idempotent. Logs the slot
// values, whether the d3d9 bodies are inline-hooked (first byte E9/EB/FF25 + owning module) and RSMods'
// presence.
bool RsxrSwap_InstallOn(void* device);
// Worker, every round once hooked: if both slots hold the original d3d9 entry points again (the
// runtime rewrote its vtable copy), patch them again; if someone else replaced a slot, leave it (a
// re-hook could close a loop through their hook) and log once. Reads are SEH-guarded.
void RsxrSwap_Recheck();
// Present callback, render thread: overlay binds are temporary and a state block restores them without
// SetTexture. Preserve the game's swap mappings and defer ramp work until its next bind.
void RsxrSwap_SetOverlayDrawing(IDirect3DDevice9* dev, bool drawing);
// RsxrRamp_UpdateV6 with this module's state (enabled = installed, deviceOk = no foreign device seen).
void RsxrSwap_UpdateV6(const char* reason);
// Worker -> render thread: release the Active entries flagged gone (RsxrCand::gone >= 2) at the next
// SetTexture call (only the render thread may Release D3D objects it bound).
void RsxrSwap_RequestRetire();

// RsxrSwap_RequestRecolour, RsxrSwapStats and RsxrSwap_Stats (overlay and settings) are declared in
// features/colours/colours.h, the feature's public header.

// Hot-patchable thunks (first 5 bytes 8B FF 55 8B EC), the values written into the slots.
extern "C" void Rsxr_Thunk_SetTexture();
extern "C" void Rsxr_Thunk_GetTexture();

// Tests.
void RsxrSwap_UninstallForTests();  // restores the slots, releases every reference we hold
HRESULT RsxrSwap_RawGetTextureForTests(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);  // the original slot
void RsxrSwap_InjectFaultForTests(bool);                                                    // Process raises 0xE0000001
// Replaces the engine revalidation: 1 = valid (the validator QIs IDirect3DTexture9 into *out),
// 0 = stale, -1 = list lock busy, 2 = not a 2D texture. nullptr: the engine (default).
using RsxrSwapValidator = int (*)(const RsxrCand& c, IDirect3DTexture9** out);
void RsxrSwap_SetValidatorForTests(RsxrSwapValidator);
