#pragma once
#include <windows.h>

// Local IIDs: the DLL links neither dxguid.lib nor d3d9.lib (it must not import d3d9.dll).
// IID_IDirect3DTexture9 {85C31227-3DE5-4f00-9B3A-F11AC38C18B5} (d3d9.h); test_d3d_hook.cpp checks it.
constexpr GUID kRs7IID_Texture9 = {0x85C31227, 0x3DE5, 0x4F00, {0x9B, 0x3A, 0xF1, 0x1A, 0xC3, 0x8C, 0x18, 0xB5}};
