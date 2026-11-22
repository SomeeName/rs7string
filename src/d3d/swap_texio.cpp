#include "d3d/swap_texio.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace rsxrswap {
namespace {
struct Locked {
    IDirect3DTexture9* tex;
    UINT level;
} g_locked;  // the level a read or write holds locked (the fault handler unlocks it)

// LockRect/UnlockRect with the level remembered, so the fault handler can unlock it.
bool LockLevel(IDirect3DTexture9* t, UINT k, D3DLOCKED_RECT* lr, DWORD flags) {
    if (FAILED(t->LockRect(k, lr, nullptr, flags))) {
        return false;
    }

    g_locked = {t, k};

    return true;
}

void UnlockLevel(IDirect3DTexture9* t, UINT k) {
    g_locked = {};
    t->UnlockRect(k);
}

// The copy loops write and compare w x h 32-bit pixels per level: the level must be a 32-bit format of exactly
// that size, and the image must hold w x h pixels. The callers build both from the same stock desc; this keeps a
// mismatch a refused copy instead of a write past the locked rows.
bool FitsLevel(IDirect3DTexture9* t, UINT k, const RsxrImage& im, char* why, size_t whyLen) {
    D3DSURFACE_DESC d{};

    if (FAILED(t->GetLevelDesc(k, &d))) {
        std::snprintf(why, whyLen, "GetLevelDesc (write) level %u failed", k);
        return false;
    }

    const bool argb = d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8;

    if (!argb || d.Width != im.w || d.Height != im.h || im.px.size() < size_t(im.w) * im.h) {
        std::snprintf(why, whyLen, "level %u is %ux%u format %u, data %ux%u (%zu px)", k, d.Width, d.Height,
                      unsigned(d.Format), im.w, im.h, im.px.size());
        return false;
    }

    return true;
}
}  // namespace

bool ReadLevels(IDirect3DTexture9* t, std::vector<RsxrImage>& out, char* why, size_t whyLen) {
    for (UINT k = 0; k < out.size(); ++k) {
        D3DSURFACE_DESC d{};
        D3DLOCKED_RECT lr{};

        if (FAILED(t->GetLevelDesc(k, &d))) {
            std::snprintf(why, whyLen, "GetLevelDesc level %u failed", k);
            return false;
        }

        if (d.Format != D3DFMT_A8R8G8B8 && d.Format != D3DFMT_X8R8G8B8) {  // rows below are read as 32-bit pixels
            std::snprintf(why, whyLen, "level %u: format %u is not 32-bit RGB", k, unsigned(d.Format));
            return false;
        }

        RsxrImage& im = out[k];

        im.w = d.Width;
        im.h = d.Height;
        im.px.assign(size_t(im.w) * im.h, 0);

        if (!LockLevel(t, k, &lr, D3DLOCK_READONLY)) {
            std::snprintf(why, whyLen, "LockRect level %u failed", k);
            return false;
        }

        const auto* src = static_cast<const uint8_t*>(lr.pBits);

        if (!src) {  // never after a successful LockRect; keeps the copy below provably non-null
            UnlockLevel(t, k);
            std::snprintf(why, whyLen, "LockRect level %u returned no bits", k);
            return false;
        }

        for (unsigned y = 0; y < im.h; ++y) {
            std::memcpy(&im.px[size_t(y) * im.w], src + size_t(y) * lr.Pitch, size_t(im.w) * 4);
        }

        UnlockLevel(t, k);
    }

    return true;
}

bool WriteAndCheck(IDirect3DTexture9* ours, const std::vector<RsxrImage>& b, char* why, size_t whyLen) {
    for (UINT k = 0; k < b.size(); ++k) {
        D3DLOCKED_RECT lr{};

        if (!FitsLevel(ours, k, b[k], why, whyLen)) {
            return false;
        }

        if (!LockLevel(ours, k, &lr, 0)) {
            std::snprintf(why, whyLen, "LockRect (write) level %u failed", k);
            return false;
        }

        auto* dst = static_cast<uint8_t*>(lr.pBits);

        if (!dst) {  // never after a successful LockRect; keeps the copy below provably non-null
            UnlockLevel(ours, k);
            std::snprintf(why, whyLen, "LockRect (write) level %u returned no bits", k);
            return false;
        }

        for (unsigned y = 0; y < b[k].h; ++y) {
            std::memcpy(dst + size_t(y) * lr.Pitch, &b[k].px[size_t(y) * b[k].w], size_t(b[k].w) * 4);
        }

        UnlockLevel(ours, k);
    }

    for (UINT k = 0; k < b.size(); ++k) {
        D3DLOCKED_RECT lr{};

        if (!LockLevel(ours, k, &lr, D3DLOCK_READONLY)) {
            std::snprintf(why, whyLen, "LockRect (read back) level %u failed", k);
            return false;
        }

        const auto* src = static_cast<const uint8_t*>(lr.pBits);

        if (!src) {  // never after a successful LockRect; keeps the compare below provably non-null
            UnlockLevel(ours, k);
            std::snprintf(why, whyLen, "LockRect (read back) level %u returned no bits", k);
            return false;
        }

        bool same = true;

        for (unsigned y = 0; y < b[k].h && same; ++y) {
            same = std::memcmp(src + size_t(y) * lr.Pitch, &b[k].px[size_t(y) * b[k].w], size_t(b[k].w) * 4) == 0;
        }

        UnlockLevel(ours, k);

        if (!same) {
            std::snprintf(why, whyLen, "read-back of level %u differs", k);
            return false;
        }
    }

    return true;
}

bool BuildChecked(const std::vector<RsxrImage>& stock, const uint32_t rgb[2], CheckedBuild& out, char* why,
                  size_t whyLen) {
    // The eighth string's band (V7) is painted with every copy: unused lines on 6/7-string guitars.
    const uint32_t rgb8[2] = {rsxrcolour::Colour8(0), rsxrcolour::Colour8(1)};

    out.build = RsxrRamp_Build(stock, rgb, rgb8);
    out.chk = RsxrRamp_Verify(stock, out.build, rgb, rgb8);

    if (!out.chk.exact || !out.chk.v6Pure) {
        std::snprintf(why, whyLen, "self-check failed: exact=%d v6-pure=%d %s", int(out.chk.exact), int(out.chk.v6Pure),
                      out.chk.why.c_str());
        return false;
    }

    return true;
}

CopyResult MakeCopy(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d, unsigned levels,
                    const std::vector<RsxrImage>& data, IDirect3DTexture9** out, char* why, size_t whyLen) {
    IDirect3DTexture9* ours = nullptr;
    const HRESULT hr = dev->CreateTexture(d.Width, d.Height, levels, 0, d.Format, D3DPOOL_MANAGED, &ours, nullptr);

    if (FAILED(hr) || !ours) {
        std::snprintf(why, whyLen, "CreateTexture: %08lX", hr);
        return CopyResult::CreateFailed;
    }

    if (!WriteAndCheck(ours, data, why, whyLen)) {
        ours->Release();
        return CopyResult::WriteFailed;
    }

    *out = ours;

    return CopyResult::Ok;
}

void UnlockAfterFault() {
    if (g_locked.tex) {
        __try {
            g_locked.tex->UnlockRect(g_locked.level);
        } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        }

        g_locked = {};
    }
}

void ForgetLocked() {
    g_locked = {};
}
}  // namespace rsxrswap
