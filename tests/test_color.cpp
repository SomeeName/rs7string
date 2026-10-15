#include "core/color.h"
#include "test.h"

TEST(color_parse_rgb_accepts_hex_forms) {
    uint32_t v = 0;

    CHECK(Rs7_ParseRgb(L"C8C8C8", &v) && v == 0xC8C8C8);
    CHECK(Rs7_ParseRgb(L"c8c8c8", &v) && v == 0xC8C8C8);
    CHECK(Rs7_ParseRgb(L"#FF00ff", &v) && v == 0xFF00FF);
    CHECK(Rs7_ParseRgb(L"  0a0B0c ", &v) && v == 0x0A0B0C);
}

TEST(color_parse_rgb_rejects_bad_text) {
    uint32_t v = 0x123456;

    CHECK(!Rs7_ParseRgb(L"", &v));
    CHECK(!Rs7_ParseRgb(L"C8C8C", &v));
    CHECK(!Rs7_ParseRgb(L"C8C8C8C8", &v));
    CHECK(!Rs7_ParseRgb(L"GGGGGG", &v));
    CHECK(!Rs7_ParseRgb(L"0xC8C8C8", &v));
    CHECK(!Rs7_ParseRgb(L"C8 C8C8", &v));
    CHECK(!Rs7_ParseRgb(L"#", &v));
    CHECK(!Rs7_ParseRgb(nullptr, &v));
    CHECK_EQ(v, 0x123456u);  // untouched on failure
}

TEST(color_rgb_to_floats_divides_by_255) {
    float f[3];

    Rs7_RgbToFloats(0xC8FF00, f);
    CHECK(f[0] == 200.0f / 255.0f && f[1] == 1.0f && f[2] == 0.0f);
}

TEST(recolor_with_the_anchor_colour_is_identity) {
    for (int a : {0x90, 0x4C}) {
        for (int g = 0; g < 256; ++g) {
            CHECK_EQ(Rs7_Recolor(uint8_t(g), uint8_t(a), uint8_t(a)), uint8_t(g));
        }
    }
}

TEST(recolor_keeps_black_and_white_and_maps_the_anchor) {
    CHECK_EQ(Rs7_Recolor(0, 0x90, 0xC8), 0);
    CHECK_EQ(Rs7_Recolor(255, 0x90, 0xC8), 255);
    CHECK_EQ(Rs7_Recolor(0x90, 0x90, 0xC8), 0xC8);
    CHECK_EQ(Rs7_Recolor(0x48, 0x90, 0xC8), 0x64);  // half the anchor -> half the colour
}
