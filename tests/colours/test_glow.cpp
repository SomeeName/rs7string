#include "features/colours/colours.h"
#include "test.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool Near(const float* a, float r, float g, float b) {
    return std::fabs(a[0] - r) < 1e-6f && std::fabs(a[1] - g) < 1e-6f && std::fabs(a[2] - b) < 1e-6f;
}

std::wstring GlowIni(const char* content) {
    wchar_t dir[MAX_PATH] = {}, path[MAX_PATH] = {};

    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"r7g", 0, path);
    FILE* f = nullptr;

    _wfopen_s(&f, path, L"wb");

    if (f) {
        std::fputs(content, f);
        std::fclose(f);
    }

    return path;
}
}  // namespace

TEST(rsmods_glow_hue_matches_rsmods_rgb2hsl) {
    const float red[3] = {128 / 255.0f, 0, 0}, green[3] = {0, 1, 0}, grey[3] = {0.5f, 0.5f, 0.5f};
    const float bluish[3] = {1, 0, 0.01f}, rose[3] = {1, 0.31f, 0.353f};
    int h = -1;

    CHECK(rs7rsmods::Hue(red, &h) && h == 0);
    CHECK(rs7rsmods::Hue(green, &h) && h == 120);
    // -0.6 truncates to 0 (RSMods' static_cast<int>), not to 359.
    CHECK(rs7rsmods::Hue(bluish, &h) && h == 0);
    CHECK(rs7rsmods::Hue(rose, &h) && h == 357);
    CHECK(!rs7rsmods::Hue(grey, &h));
}

TEST(rsmods_glow_hsl2rgb_negative_hue_keeps_the_stale_colour) {
    float out[3] = {0.1f, 0.2f, 0.3f};

    // RSMods' Glow for hue 0 is HSL(-4, 1, 0.5): no sextant, the previous colour stays (the bug).
    rs7rsmods::HslToRgb(-4.0f, 1.0f, 0.5f, out);
    CHECK(Near(out, 0.1f, 0.2f, 0.3f));
    rs7rsmods::HslToRgb(116.0f, 1.0f, 0.5f, out);
    CHECK(Near(out, 1 - (116 / 60.0f - 1), 1, 0));
}

TEST(rsmods_glow_pure_red_gets_the_wrapped_glow) {
    const float red[3] = {128 / 255.0f, 0, 0}, stale[3] = {0.02f, 0.01f, 0.0f};
    float out[3] = {};

    CHECK(rs7rsmods::CorrectGlow(true, red, stale, out));
    // HSL(356, 1, 0.5): sextant 5, (v, min, v - v*fract).
    CHECK(Near(out, 1, 0, 1 - (356 / 60.0f - 5)));
}

TEST(rsmods_glow_other_hues_and_no_rsmods_are_unchanged) {
    const float green[3] = {0, 1, 0}, glow[3] = {0.4f, 0.5f, 0.6f}, red[3] = {1, 0, 0};
    const float grey[3] = {0.5f, 0.5f, 0.5f}, hue4[3] = {1, 4 / 60.0f + 1e-4f, 0};
    float out[3] = {};
    float rsmods[3] = {};

    CHECK(!rs7rsmods::CorrectGlow(true, green, glow, out));
    CHECK(Near(out, 0.4f, 0.5f, 0.6f));
    // RSMods' own glow for hue 120 is what our formula would give: nothing to correct there.
    rs7rsmods::HslToRgb(116.0f, 1.0f, 0.5f, rsmods);
    CHECK(Near(rsmods, 1 - (116 / 60.0f - 1), 1, 0));
    CHECK(!rs7rsmods::CorrectGlow(false, red, glow, out));
    CHECK(Near(out, 0.4f, 0.5f, 0.6f));
    CHECK(!rs7rsmods::CorrectGlow(true, grey, glow, out));
    CHECK(!rs7rsmods::CorrectGlow(true, hue4, glow, out));
}

TEST(rsmods_glow_ini_needs_custom_colours_without_exact_shading) {
    struct {
        const char* ini;
        bool want;
    } cases[] = {
        {"[Toggle Switches]\r\nCustomStringColors=2\r\nUseExactColors=off\r\n", true},
        {"[Toggle Switches]\r\nCustomStringColors=2\r\n", true},
        {"[Toggle Switches]\r\nCustomStringColors=2\r\nUseExactColors=on\r\n", false},
        {"[Toggle Switches]\r\nCustomStringColors=1\r\n", false},
        {"[Toggle Switches]\r\nCustomStringColors=0\r\n", false},
        {"", false},
    };

    for (const auto& c : cases) {
        const auto p = GlowIni(c.ini);

        CHECK_EQ(rs7rsmods::ReadCustomGlow(p.c_str()), c.want);
        DeleteFileW(p.c_str());
    }

    CHECK(!rs7rsmods::ReadCustomGlow(nullptr));
}

TEST(rsmods_glow_pointer_reads_enabled_in_both_layouts) {
    const uintptr_t savedFix = g_rs7_glow_fix, savedNative = g_rs7_native_colours;
    std::vector<uint8_t> mgr(rs7colour::kSize);
    float red[3] = {128 / 255.0f, 0, 0}, green[3] = {0, 1, 0}, glow[3] = {0.02f, 0.01f, 0};

    for (uintptr_t native = 0; native < 2; ++native) {
        g_rs7_native_colours = native;
        std::memset(mgr.data(), 0, mgr.size());
        *reinterpret_cast<uint32_t*>(&mgr[0x348]) = 1;  // colourblind mode
        // Enabled (state 1) of string 0 red, string 1 green: stock 368h + mode*2A0h, native DF0h + mode*380h.
        const uint32_t enabled0 = rs7colour::PtrOffset(1, 1, 0);

        CHECK_EQ(enabled0, native ? 0xDF0u + 0x380u : 0x368u + 0x2A0u);
        *reinterpret_cast<float**>(&mgr[enabled0]) = red;
        *reinterpret_cast<float**>(&mgr[enabled0 + 4]) = green;
        g_rs7_glow_fix = 0;
        CHECK(Rs7_GlowFixPtr(glow, mgr.data(), 0) == glow);
        g_rs7_glow_fix = 1;
        const float* fixed = Rs7_GlowFixPtr(glow, mgr.data(), 0);

        CHECK(fixed != glow && Near(fixed, 1, 0, 1 - (356 / 60.0f - 5)));
        CHECK(Rs7_GlowFixPtr(glow, mgr.data(), 1) == glow);
        CHECK(Rs7_GlowFixPtr(glow, mgr.data(), 6) == glow);  // strings 6/7 are rs7string's own colours
        *reinterpret_cast<uint32_t*>(&mgr[0x348]) = 2;
        CHECK(Rs7_GlowFixPtr(glow, mgr.data(), 0) == glow);
    }

    g_rs7_glow_fix = savedFix;
    g_rs7_native_colours = savedNative;
}
