#pragma once
#include "d3d/swap.h"
#include "features/colours/colours.h"
#include <windows.h>
#include <d3d9.h>

// The stock colour ramps (swap_ramp.cpp): candidates found by the worker are copied and bound on the render
// thread, repainted when the colour changes and released when the engine drops them.
namespace rs7swap {
struct Copy {
    uint64_t hash;
    uint32_t fmt;
    IDirect3DTexture9* tex;  // we own a reference
    uint32_t colourGeneration;
};

struct RampState {
    Rs7SwapCfg cfg = {{kRs7DefaultColour, kRs7DefaultColour}};
    CRITICAL_SECTION* listLock;  // the game's NiTexture list lock
    uintptr_t d3d9Lo, d3d9Hi;    // d3d9.dll's image
    Rs7SwapValidator validator;  // tests: replaces the engine revalidation
    volatile bool injectFault;
    volatile LONG retireReq;
    volatile LONG recolourReq;  // Rs7Swap_RequestRecolour -> the render thread (OnSet)
    volatile LONG recolourRgb[2] = {LONG(kRs7DefaultColour), LONG(kRs7DefaultColour)};
    // Render thread only.
    Copy copies[kRs7MaxCands];
    uint32_t rgb8[2] = {0xFFFFFFFF, 0xFFFFFFFF};  // the eighth string's colours of the last Recolour (none yet)
    unsigned copyCount;
    uint32_t colourGeneration;
    bool recolourFailed;
    DWORD recolourRetryAtMs;
    Rs7Cand* processing;  // the candidate Process works on (fault handler)
};

extern RampState g_ramp;

// Render thread, every SetTexture on the hooked device: the texture to bind in place of `tex`.
IDirect3DBaseTexture9* OnSet(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
// After a fault inside OnSet: unlock what Process left locked, refuse the candidate it worked on.
void AfterFault();
// Tests: release every reference and clear the request flags.
void ResetRamp();
}  // namespace rs7swap
