#pragma once
#include "core/ramp.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <vector>

// Real D3D9 devices for test_d3d.cpp / test_gpu.cpp: a hidden window, windowed HAL with software vertex
// processing (REF if HAL fails). FPU_PRESERVE keeps the process's x87 precision for the other tests.
namespace rs7d3d {
inline HWND Window() {
    static HWND w = [] {
        WNDCLASSW wc{};

        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"rs7string_tests";
        RegisterClassW(&wc);

        return CreateWindowExW(0, wc.lpszClassName, L"rs7string tests", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                               nullptr, wc.hInstance, nullptr);
    }();

    return w;
}

struct Device {
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* dev = nullptr;
    bool ref = false;

    Device() {
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) {
            return;
        }

        D3DPRESENT_PARAMETERS pp{};

        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.hDeviceWindow = Window();
        const DWORD flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;

        if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window(), flags, &pp, &dev))) {
            dev = nullptr;
            if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, Window(), flags, &pp, &dev))) {
                ref = true;
            } else {
                dev = nullptr;
            }
        }

        static bool told = false;

        if (dev && !told) {
            told = true;
            D3DADAPTER_IDENTIFIER9 id{};

            d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
            std::printf("  d3d9: %s device on '%s'\n", ref ? "REF" : "HAL", id.Description);
        }
    }

    ~Device() {
        if (dev) {
            dev->Release();
        }
        if (d3d) {
            d3d->Release();
        }
    }

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    explicit operator bool() const { return dev != nullptr; }
};

// true: go on; false: printed "skip: no D3D9 device" (the test passes without checks).
inline bool Have(const Device& d) {
    if (d) {
        return true;
    }

    std::printf("  skip: no D3D9 device\n");

    return false;
}

// A texture with `lv`'s level sizes; MANAGED/SYSTEMMEM levels filled from lv (32-bit formats).
inline IDirect3DTexture9* MakeTex(IDirect3DDevice9* dev, const std::vector<Rs7Image>& lv,
                                  D3DFORMAT fmt = D3DFMT_X8R8G8B8, D3DPOOL pool = D3DPOOL_MANAGED) {
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(lv[0].w, lv[0].h, UINT(lv.size()), 0, fmt, pool, &t, nullptr))) {
        return nullptr;
    }
    if (pool == D3DPOOL_DEFAULT) {
        return t;
    }

    for (UINT k = 0; k < lv.size(); ++k) {
        D3DLOCKED_RECT lr{};
        if (FAILED(t->LockRect(k, &lr, nullptr, 0))) {
            continue;
        }

        for (unsigned y = 0; y < lv[k].h; ++y) {
            std::memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch, &lv[k].px[size_t(y) * lv[k].w],
                        size_t(lv[k].w) * 4);
        }

        t->UnlockRect(k);
    }

    return t;
}

// All levels of a 32-bit texture (empty on failure).
inline std::vector<Rs7Image> ReadTex(IDirect3DTexture9* t) {
    std::vector<Rs7Image> v;

    for (UINT k = 0; k < t->GetLevelCount(); ++k) {
        D3DSURFACE_DESC d{};
        D3DLOCKED_RECT lr{};
        if (FAILED(t->GetLevelDesc(k, &d)) || FAILED(t->LockRect(k, &lr, nullptr, D3DLOCK_READONLY))) {
            return {};
        }

        Rs7Image im;

        im.w = d.Width;
        im.h = d.Height;
        im.px.resize(size_t(im.w) * im.h);

        for (unsigned y = 0; y < im.h; ++y) {
            std::memcpy(&im.px[size_t(y) * im.w], static_cast<const uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch,
                        size_t(im.w) * 4);
        }

        t->UnlockRect(k);
        v.push_back(im);
    }

    return v;
}

inline bool SameImages(const std::vector<Rs7Image>& a, const std::vector<Rs7Image>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t k = 0; k < a.size(); ++k) {
        if (a[k].w != b[k].w || a[k].h != b[k].h || a[k].px != b[k].px) {
            return false;
        }
    }

    return true;
}
}  // namespace rs7d3d
