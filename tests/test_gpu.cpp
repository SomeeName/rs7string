// GPU equivalence (plan Task 10): every sample the game can take of a stock ramp, drawn on the real GPU
// from the stock texture and from our copy, must give the same pixels. This retires the "hardware
// filter" part of the bit-exactness claim offline. A failure here is a hardware finding: it prints the
// failing (k, filter, V, x) and both pixel values.
#include "core/ramp.h"
#include "d3d_fixture.h"
#include "ramp_fixture.h"
#include "test.h"
#include <cstdio>
#include <vector>

namespace {
using rs7d3d::Device;
const uint32_t kRgb[2] = {0xC8C8C8, 0x00FFFF};
constexpr unsigned kW = 256;
constexpr float kU0 = 1.0f / (3.0f * kW);  // 1/3 texel at level 0: non-trivial horizontal weights

struct Vtx {
    float x, y, z, rhw, u, v;
};

// 256x1 X8R8G8B8 render target and its SYSTEMMEM twin for read-back.
struct Target {
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* sys = nullptr;

    explicit Target(IDirect3DDevice9* dev) {
        dev->CreateRenderTarget(kW, 1, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr);
        dev->CreateOffscreenPlainSurface(kW, 1, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    }

    ~Target() {
        if (rt) {
            rt->Release();
        }
        if (sys) {
            sys->Release();
        }
    }

    explicit operator bool() const { return rt && sys; }
};

void FixedFunction(IDirect3DDevice9* dev) {
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
}

// One 256x1 quad sampling `tex` at constant V on level k (MAXMIPLEVEL = k: magnification gives LOD < k,
// so the GPU clamps to k), U from 1/3 texel to 1 + 1/3 texel. Returns the 256 pixels (RGB; X8 alpha
// masked off), empty on failure.
std::vector<uint32_t> Render(IDirect3DDevice9* dev, const Target& t, IDirect3DTexture9* tex, DWORD k,
                             D3DTEXTUREFILTERTYPE filter, float v) {
    std::vector<uint32_t> px;
    if (FAILED(dev->SetRenderTarget(0, t.rt))) {
        return px;
    }

    FixedFunction(dev);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, k);
    dev->SetTexture(0, tex);
    const Vtx q[4] = {{-0.5f, -0.5f, 0.0f, 1.0f, kU0, v},
                      {kW - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f + kU0, v},
                      {-0.5f, 0.5f, 0.0f, 1.0f, kU0, v},
                      {kW - 0.5f, 0.5f, 0.0f, 1.0f, 1.0f + kU0, v}};

    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0f, 0);
    if (FAILED(dev->BeginScene())) {
        return px;
    }

    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));

    dev->EndScene();
    dev->SetTexture(0, nullptr);
    if (FAILED(hr) || FAILED(dev->GetRenderTargetData(t.rt, t.sys))) {
        return px;
    }

    D3DLOCKED_RECT lr{};
    if (FAILED(t.sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        return px;
    }

    const auto* p = static_cast<const uint32_t*>(lr.pBits);

    px.assign(p, p + kW);
    t.sys->UnlockRect();

    for (auto& c : px) {
        c &= 0x00FFFFFFu;
    }

    return px;
}

const char* FilterName(D3DTEXTUREFILTERTYPE f) {
    return f == D3DTEXF_POINT ? "POINT" : "LINEAR";
}

// Compares two renders; prints the first difference (a hardware finding) and returns false.
bool Same(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, const char* what, DWORD k,
          D3DTEXTUREFILTERTYPE f, float v) {
    if (a.size() != kW || b.size() != kW) {
        std::printf("  render failed: %s k=%lu %s V=%.9g\n", what, k, FilterName(f), double(v));
        return false;
    }

    for (unsigned x = 0; x < kW; ++x) {
        if (a[x] != b[x]) {
            std::printf("  DIFF %s: k=%lu filter=%s V=%.9g x=%u: %06X vs %06X\n", what, k, FilterName(f), double(v), x,
                        a[x], b[x]);
            return false;
        }
    }

    return true;
}

struct Pair {
    std::vector<Rs7Image> stock;
    Rs7RampBuild build;
    IDirect3DTexture9* s = nullptr;
    IDirect3DTexture9* o = nullptr;

    Pair(IDirect3DDevice9* dev, std::vector<Rs7Image> lv) : stock(std::move(lv)), build(Rs7Ramp_Build(stock, kRgb)) {
        s = rs7d3d::MakeTex(dev, stock);
        o = rs7d3d::MakeTex(dev, build.levels);
    }

    ~Pair() {
        if (s) {
            s->Release();
        }
        if (o) {
            o->Release();
        }
    }

    explicit operator bool() const { return s && o; }
};

const D3DTEXTUREFILTERTYPE kFilters[2] = {D3DTEXF_POINT, D3DTEXF_LINEAR};
}  // namespace

TEST(gpu_stock_rows_are_identical_on_every_level_point_and_linear) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Target t(d.dev);

    CHECK(bool(t));
    if (!t) {
        return;
    }

    for (int variant = 0; variant < 2; ++variant) {
        Pair p(d.dev, variant ? rs7fix::UniformStock(9) : rs7fix::Stock(9));

        CHECK(bool(p));
        if (!p) {
            return;
        }

        if (variant == 0) {  // not vacuous: MAXMIPLEVEL really selects level k (the levels' pixels differ)
            for (DWORD k = 1; k < 9; ++k) {
                CHECK(Render(d.dev, t, p.s, k, D3DTEXF_POINT, 0.5f) !=
                      Render(d.dev, t, p.s, k - 1, D3DTEXF_POINT, 0.5f));
            }
        }

        unsigned fails = 0;

        for (DWORD k = 0; k < 9 && fails < 3; ++k) {
            for (auto f : kFilters) {
                for (unsigned i = 0; i < 16; ++i) {
                    const float v = (i + 0.5f) / 16.0f;

                    if (!Same(Render(d.dev, t, p.s, k, f, v), Render(d.dev, t, p.o, k, f, v),
                              variant ? "uniform stock vs copy" : "worst-case stock vs copy", k, f, v)) {
                        CHECK(!"stock row differs on the GPU");
                        ++fails;
                    }
                }
            }
        }
    }
}

TEST(gpu_preview_row_is_identical) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Target t(d.dev);
    Pair p(d.dev, rs7fix::Stock(9));

    CHECK(bool(t) && bool(p));
    if (!t || !p) {
        return;
    }

    for (DWORD k = 0; k < 9; ++k) {
        for (auto f : kFilters) {
            for (float v : {7.5f / 16.0f, 15.5f / 16.0f}) {
                CHECK(Same(Render(d.dev, t, p.s, k, f, v), Render(d.dev, t, p.o, k, f, v), "preview row", k, f, v));
            }
        }
    }
}

TEST(gpu_v6_samples_the_custom_band_on_levels_0_and_1) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Target t(d.dev);
    Pair p(d.dev, rs7fix::Stock(9));

    CHECK(bool(t) && bool(p));
    if (!t || !p) {
        return;
    }

    CHECK_EQ(p.build.customLevels, 2u);

    for (DWORD k = 0; k < 2; ++k) {
        for (int m = 0; m < 2; ++m) {
            // The line V6 lands on at level k (both taps hold the same recoloured line), alone in a
            // W_k x 1 helper sampled at V = 0.5: only the U filter remains.
            const Rs7Image& lv = p.build.levels[k];
            const unsigned line = Rs7Ramp_Taps(kRs7V6Num[m], kRs7V6Den, lv.h, false)[0].line;
            Rs7Image h;

            h.w = lv.w;
            h.h = 1;
            h.px.assign(lv.px.begin() + size_t(line) * lv.w, lv.px.begin() + size_t(line + 1) * lv.w);
            IDirect3DTexture9* helper = rs7d3d::MakeTex(d.dev, {h});

            CHECK(helper != nullptr);
            if (!helper) {
                continue;
            }

            for (auto f : kFilters) {
                CHECK(Same(Render(d.dev, t, p.o, k, f, kRs7V6[m]), Render(d.dev, t, helper, 0, f, 0.5f),
                           m ? "V6 colourblind band" : "V6 band", k, f, kRs7V6[m]));
                // not vacuous: the stock at V6 shows something else (the band is really ours)
                CHECK(Render(d.dev, t, p.s, k, f, kRs7V6[m]) != Render(d.dev, t, p.o, k, f, kRs7V6[m]));
            }

            helper->Release();
        }
    }
}

TEST(gpu_v6_on_the_stock_texture_reads_the_preview_row_on_level_0) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    Target t(d.dev);
    Pair p(d.dev, rs7fix::UniformStock(9));

    CHECK(bool(t) && bool(p));
    if (!t || !p) {
        return;
    }

    for (int m = 0; m < 2; ++m) {
        for (auto f : kFilters) {
            const float preview = (kRs7Row6[m] + 0.5f) / 16.0f;

            CHECK(Same(Render(d.dev, t, p.s, 0, f, kRs7V6[m]), Render(d.dev, t, p.s, 0, f, preview),
                       "unswapped stock at V6 vs preview row", 0, f, kRs7V6[m]));
        }
    }
}
