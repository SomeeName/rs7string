// d3d/swap_texio.cpp WriteAndCheck: the copy is written only into a 32-bit level of exactly the image's size.
#include "d3d/swap_texio.h"
#include "d3d_fixture.h"
#include "test.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
Rs7Image Image(unsigned w, unsigned h) {
    Rs7Image im;

    im.w = w;
    im.h = h;
    im.px.assign(size_t(w) * h, 0xFF336699u);

    return im;
}

bool Write(IDirect3DDevice9* dev, D3DFORMAT fmt, const Rs7Image& im, char* why, size_t whyLen) {
    IDirect3DTexture9* t = nullptr;

    if (FAILED(dev->CreateTexture(16, 8, 1, 0, fmt, D3DPOOL_MANAGED, &t, nullptr)) || !t) {
        std::snprintf(why, whyLen, "CreateTexture failed");
        return false;
    }

    const bool ok = rs7swap::WriteAndCheck(t, {im}, why, whyLen);

    t->Release();

    return ok;
}
}  // namespace

TEST(d3d_write_and_check_accepts_a_matching_level) {
    rs7d3d::Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    char why[160] = "";

    CHECK(Write(d.dev, D3DFMT_A8R8G8B8, Image(16, 8), why, sizeof why));
}

TEST(d3d_write_and_check_refuses_a_size_or_format_mismatch) {
    rs7d3d::Device d;
    if (!rs7d3d::Have(d)) {
        return;
    }

    char why[160] = "";

    CHECK(!Write(d.dev, D3DFMT_A8R8G8B8, Image(32, 8), why, sizeof why));  // wider rows than the level
    CHECK(std::strstr(why, "level 0 is 16x8") != nullptr);
    CHECK(!Write(d.dev, D3DFMT_A8R8G8B8, Image(16, 16), why, sizeof why));  // more rows than the level
    CHECK(!Write(d.dev, D3DFMT_R5G6B5, Image(16, 8), why, sizeof why));     // 16-bit level, 32-bit rows

    Rs7Image shortData = Image(16, 8);

    shortData.px.resize(8);
    CHECK(!Write(d.dev, D3DFMT_A8R8G8B8, shortData, why, sizeof why));  // fewer pixels than w x h
}
