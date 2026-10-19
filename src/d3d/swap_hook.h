#pragma once
#include "d3d/sehread.h"
#include <windows.h>
#include <d3d9.h>

// The vtable hook of the ramp swap (swap_hook.cpp): which device is hooked and what GetTexture hides. The
// ramp and RSMods-ramp parts read it and fill the bound table; they never touch the vtable.
namespace rs7swap {
constexpr unsigned kStages = 16;
using SetTex = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
using GetTex = HRESULT(__stdcall*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);

// The copy bound on a stage and the stock texture it stands in for (GetTexture returns the stock).
struct Bound {
    IDirect3DBaseTexture9* stock;
    IDirect3DBaseTexture9* ours;
};

struct HookState {
    SetTex origSet;
    GetTex origGet;
    IDirect3DDevice9* dev;  // the device we hooked
    void** vt;              // its vtable
    volatile bool installed;
    volatile bool deviceOk = true;  // false (sticky) once a candidate is bound on another device
    Bound bound[kStages];           // render thread only
    const Rs7SehReader mem{};
};

extern HookState g_hook;
}  // namespace rs7swap
