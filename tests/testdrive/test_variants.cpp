#include "features/testdrive/testdrive.h"
#include "test.h"
#include <cstring>

TEST(variant_headstock_text) {
    CHECK(Rs7Variant_ParseHeadstock(L"") == 0);
    CHECK(Rs7Variant_ParseHeadstock(nullptr) == 0);
    CHECK(Rs7Variant_ParseHeadstock(L"0") == 0);
    CHECK(Rs7Variant_ParseHeadstock(L"33") == 33);
    CHECK(Rs7Variant_ParseHeadstock(L"3+3") == 33);
    CHECK(Rs7Variant_ParseHeadstock(L"6") == 6);
    CHECK(Rs7Variant_ParseHeadstock(L"Inline") == 6);
    CHECK(Rs7Variant_ParseHeadstock(L"6-IN-LINE") == 6);
    CHECK(Rs7Variant_ParseHeadstock(L"7") == -1);
}

TEST(variant_off_keeps_the_profile) {
    const Rs7Variant off;
    const Rs7GuitarFlags profile{1, 0, 1};
    const Rs7GuitarFlags out = Rs7Variant_Apply(off, profile);

    CHECK(!off.Any());
    CHECK(out.lefty == 1 && out.inlineHead == 0 && out.colourblind == 1);

    const char* text = "ColorBlindNone";

    CHECK(Rs7Variant_ColourModeText(off, text) == text);
}

TEST(variant_forces_each_flag) {
    Rs7Variant v;

    v.lefty = true;
    v.headstock = 6;
    CHECK(v.Any());

    Rs7GuitarFlags out = Rs7Variant_Apply(v, Rs7GuitarFlags{0, 0, 0});

    CHECK(out.lefty == 1 && out.inlineHead == 1 && out.colourblind == 0);

    v = Rs7Variant{};
    v.headstock = 33;
    v.colourblind = true;
    out = Rs7Variant_Apply(v, Rs7GuitarFlags{0, 1, 0});
    CHECK(out.lefty == 0 && out.inlineHead == 0 && out.colourblind == 1);
    CHECK(!std::strcmp(Rs7Variant_ColourModeText(v, "ColorBlindNone"), "ColorBlindMild"));
}
