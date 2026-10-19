// RSMods' own note ramps: the string 6 copy, pass-through cases, the cache limit.
#include "d3d/swap.h"
#include "swap_fixture.h"
#include "test.h"
#include <string>
#include <vector>

using namespace rs7swapfix;

namespace {
// RSMods-like ramp: uniform rows, rows 7/15 grey, A8R8G8B8 (differs from UniformStock in rows 0-5).
std::vector<Rs7Image> RsmodsRamp() {
    auto lv = rs7fix::UniformStock(1);

    for (unsigned y = 0; y < 48; ++y) {
        for (unsigned x = 0; x < lv[0].w; ++x) {
            lv[0].px[size_t(y) * lv[0].w + x] ^= 0x00400000u;
        }
    }

    return lv;
}

// Begin + an active stock candidate on stage 1 (V6 on); returns the stock texture (caller releases).
IDirect3DTexture9* BeginWithStock(IDirect3DDevice9* dev, bool rsmods) {
    Rs7SwapCfg cfg = kCfg;

    cfg.rsmods = rsmods;
    Begin(cfg);
    IDirect3DTexture9* stock = rs7d3d::MakeTex(dev, rs7fix::UniformStock(1));

    Add(stock);
    Rs7Swap_InstallOn(dev);
    dev->SetTexture(1, stock);

    return stock;
}
}  // namespace

TEST(d3d_rsmods_ramp_gets_a_string6_copy_and_get_texture_hides_it) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    IDirect3DTexture9* stock = BeginWithStock(d.dev, true);

    CHECK(V6On());
    const auto lv = RsmodsRamp();
    IDirect3DTexture9* rsm = rs7d3d::MakeTex(d.dev, lv, D3DFMT_A8R8G8B8);
    Capturing cap;

    d.dev->SetTexture(1, rsm);  // RSMods binds its ramp after the game's
    IDirect3DBaseTexture9* raw = RawBound(d.dev, 1);

    CHECK(raw != nullptr && raw != static_cast<IDirect3DBaseTexture9*>(rsm));
    IDirect3DTexture9* ours = nullptr;

    if (raw) {
        raw->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&ours));
    }

    if (ours) {
        D3DSURFACE_DESC dd{};

        ours->GetLevelDesc(0, &dd);
        CHECK(dd.Format == D3DFMT_A8R8G8B8 && dd.Pool == D3DPOOL_MANAGED);
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(lv)));
        ours->Release();
    }

    CHECK(Logged("ramp: RSMods ramp", "with string 6 C8C8C8 / 00FFFF"));
    IDirect3DBaseTexture9* t = nullptr;

    d.dev->GetTexture(1, &t);
    CHECK(t == static_cast<IDirect3DBaseTexture9*>(rsm));  // RSMods sees its own texture
    if (t) {
        t->Release();
    }
    // Bound again: the cached copy, no second build.
    Lines().clear();
    d.dev->SetTexture(1, stock);
    d.dev->SetTexture(1, rsm);
    CHECK(RawBound(d.dev, 1) == raw);
    CHECK(!Logged("ramp: RSMods ramp"));
    // A new colour rebuilds it at the next bind.
    const uint32_t next[2] = {0x00C68E, 0x00C68E};
    Rs7SwapCfg nextCfg = kCfg;

    nextCfg.rgb[0] = next[0];
    nextCfg.rgb[1] = next[1];
    Rs7Swap_RequestRecolour(next);
    d.dev->SetTexture(1, rsm);
    ours = nullptr;
    if (IDirect3DBaseTexture9* r2 = RawBound(d.dev, 1)) {
        r2->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&ours));
    }
    CHECK(ours != nullptr && static_cast<IDirect3DBaseTexture9*>(ours) != static_cast<IDirect3DBaseTexture9*>(rsm));

    if (ours) {
        CHECK(rs7d3d::SameImages(rs7d3d::ReadTex(ours), Expected(lv, nextCfg)));
        ours->Release();
    }

    End(d.dev);
    CHECK_EQ(rsm->Release(), 0ul);  // every reference we took is gone again
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_rsmods_ramp_without_rsmods_or_v6_passes_through) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    IDirect3DTexture9* stock = BeginWithStock(d.dev, false);
    IDirect3DTexture9* rsm = rs7d3d::MakeTex(d.dev, RsmodsRamp(), D3DFMT_A8R8G8B8);
    Capturing cap;

    d.dev->SetTexture(1, rsm);
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(rsm));
    CHECK(!Logged("RSMods ramp"));
    End(d.dev);
    CHECK_EQ(stock->Release(), 0ul);

    // RSMods present, but V6 is off (no stock ramp active): untouched as well.
    Rs7SwapCfg cfg = kCfg;

    cfg.rsmods = true;
    Begin(cfg);
    IDirect3DTexture9* other = rs7d3d::MakeTex(d.dev, Stock(1));

    Add(other);  // a candidate exists, but it was never bound
    Rs7Swap_InstallOn(d.dev);
    d.dev->SetTexture(1, rsm);
    CHECK(V6Off());
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(rsm));
    CHECK(!Logged("RSMods ramp"));
    End(d.dev);
    CHECK_EQ(other->Release(), 0ul);
    CHECK_EQ(rsm->Release(), 0ul);
}

TEST(d3d_rsmods_non_ramp_textures_are_left_alone_and_read_once) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    IDirect3DTexture9* stock = BeginWithStock(d.dev, true);
    IDirect3DTexture9* lines = rs7d3d::MakeTex(d.dev, Stock(1), D3DFMT_A8R8G8B8);  // every line differs
    IDirect3DTexture9* mips2 = rs7d3d::MakeTex(d.dev, Stock(2), D3DFMT_A8R8G8B8);  // 256x128 with 2 levels
    std::vector<Rs7Image> big = Stock(1);

    big[0].w = 512;
    big[0].px.resize(size_t(512) * 128);
    IDirect3DTexture9* wide = rs7d3d::MakeTex(d.dev, big, D3DFMT_A8R8G8B8);
    Capturing cap;

    IDirect3DTexture9* x8 = rs7d3d::MakeTex(d.dev, RsmodsRamp());  // X8R8G8B8: a game ramp, not RSMods'

    d.dev->SetTexture(1, x8);
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(x8));
    d.dev->SetTexture(1, lines);
    d.dev->SetTexture(1, lines);
    d.dev->SetTexture(1, wide);
    d.dev->SetTexture(2, mips2);  // another stage: never looked at
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(wide));
    CHECK(RawBound(d.dev, 2) == static_cast<IDirect3DBaseTexture9*>(mips2));
    size_t refused = 0;

    for (const auto& l : Lines()) {
        refused += l.find("left alone: not a ramp with grey rows 7/15") != std::string::npos;
    }

    CHECK_EQ(refused, size_t(1));
    End(d.dev);
    CHECK_EQ(lines->Release(), 0ul);
    CHECK_EQ(x8->Release(), 0ul);
    CHECK_EQ(mips2->Release(), 0ul);
    CHECK_EQ(wide->Release(), 0ul);
    CHECK_EQ(stock->Release(), 0ul);
}

TEST(d3d_rsmods_too_many_ramps_turn_the_path_off) {
    Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    IDirect3DTexture9* stock = BeginWithStock(d.dev, true);
    std::vector<IDirect3DTexture9*> made;
    Capturing cap;

    for (int i = 0; i < 34; ++i) {
        made.push_back(rs7d3d::MakeTex(d.dev, RsmodsRamp(), D3DFMT_A8R8G8B8));
        d.dev->SetTexture(1, made.back());
    }

    CHECK(Logged("ramp: more than 32 RSMods note ramps"));
    CHECK(RawBound(d.dev, 1) == static_cast<IDirect3DBaseTexture9*>(made.back()));
    End(d.dev);

    for (auto* t : made) {
        CHECK_EQ(t->Release(), 0ul);
    }

    CHECK_EQ(stock->Release(), 0ul);
}
