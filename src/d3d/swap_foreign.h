#pragma once
#include <windows.h>
#include <d3d9.h>

// RSMods' own note ramps (swap_foreign.cpp): copies with string 6's lines repainted, cached per RSMods texture.
namespace rs7swap {
// Render thread, a bind of `tex` that is not a stock candidate: on stage 1, with RSMods present and V6 on, an
// RSMods ramp gets its copy bound instead (and recorded in the bound table). Returns what to bind.
IDirect3DBaseTexture9* OnForeign(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex);
// Releases every cached copy and starts counting afresh (the colour changed, or tests).
void ResetForeign();
}  // namespace rs7swap
