#include "features/testdrive/testdrive.h"
#include "test.h"
#include <cstring>

TEST(variant_headstock_text) {
    CHECK(RsxrVariant_ParseHeadstock(L"") == 0);
    CHECK(RsxrVariant_ParseHeadstock(nullptr) == 0);
    CHECK(RsxrVariant_ParseHeadstock(L"0") == 0);
    CHECK(RsxrVariant_ParseHeadstock(L"33") == 33);
    CHECK(RsxrVariant_ParseHeadstock(L"3+3") == 33);
    CHECK(RsxrVariant_ParseHeadstock(L"6") == 6);
    CHECK(RsxrVariant_ParseHeadstock(L"Inline") == 6);
    CHECK(RsxrVariant_ParseHeadstock(L"6-IN-LINE") == 6);
    CHECK(RsxrVariant_ParseHeadstock(L"7") == -1);
}

TEST(variant_off_keeps_the_profile) {
    const RsxrVariant off;
    const RsxrGuitarFlags profile{1, 0, 1};
    const RsxrGuitarFlags out = RsxrVariant_Apply(off, profile);

    CHECK(!off.Any());
    CHECK(out.lefty == 1 && out.inlineHead == 0 && out.colourblind == 1);

    const char* text = "ColorBlindNone";

    CHECK(RsxrVariant_ColourModeText(off, text) == text);
}

TEST(variant_forces_each_flag) {
    RsxrVariant v;

    v.lefty = true;
    v.headstock = 6;
    CHECK(v.Any());

    RsxrGuitarFlags out = RsxrVariant_Apply(v, RsxrGuitarFlags{0, 0, 0});

    CHECK(out.lefty == 1 && out.inlineHead == 1 && out.colourblind == 0);

    v = RsxrVariant{};
    v.headstock = 33;
    v.colourblind = true;
    out = RsxrVariant_Apply(v, RsxrGuitarFlags{0, 1, 0});
    CHECK(out.lefty == 0 && out.inlineHead == 0 && out.colourblind == 1);
    CHECK(!std::strcmp(RsxrVariant_ColourModeText(v, "ColorBlindNone"), "ColorBlindMild"));
}

TEST(variant_colour_mode_per_frame) {
    RsxrVariant v;

    CHECK(RsxrVariant_ColourMode(v, 0) == 0);
    CHECK(RsxrVariant_ColourMode(v, 1) == 1);
    v.colourblind = true;
    CHECK(RsxrVariant_ColourMode(v, 0) == 1);
    CHECK(RsxrVariant_ColourMode(v, 1) == 1);
}

TEST(variant_force_righty_clears_a_left_handed_profile) {
    RsxrVariant v;

    v.righty = true;
    CHECK(v.Any());
    const RsxrGuitarFlags out = RsxrVariant_Apply(v, RsxrGuitarFlags{1, 1, 1});

    CHECK(out.lefty == 0 && out.inlineHead == 1 && out.colourblind == 1);
}

TEST(variant_parse_force) {
    CHECK(RsxrVariant_ParseForce(L"", L"lh", L"rh") == 0 && RsxrVariant_ParseForce(L"0", L"lh", L"rh") == 0);
    CHECK(RsxrVariant_ParseForce(nullptr, L"lh", L"rh") == 0);
    CHECK(RsxrVariant_ParseForce(L"1", L"lh", L"rh") == 1 && RsxrVariant_ParseForce(L"LH", L"lh", L"rh") == 1);
    CHECK(RsxrVariant_ParseForce(L"-1", L"lh", L"rh") == -1 && RsxrVariant_ParseForce(L"rh", L"lh", L"rh") == -1);
    CHECK(RsxrVariant_ParseForce(L"2", L"lh", L"rh") == -2 && RsxrVariant_ParseForce(L"left", L"lh", L"rh") == -2);
    CHECK(RsxrVariant_ParseForce(L"off", L"on", L"off") == -1 && RsxrVariant_ParseForce(L"rh", L"on", L"off") == -2);
}

TEST(variant_force_normal_colours) {
    RsxrVariant v;

    v.normalColours = true;
    CHECK(v.Any());
    CHECK(RsxrVariant_Apply(v, RsxrGuitarFlags{1, 0, 1}).colourblind == 0);
    CHECK(!std::strcmp(RsxrVariant_ColourModeText(v, "ColorBlindMild"), "ColorBlindNone"));
    CHECK(RsxrVariant_ColourMode(v, 1) == 1);  // no per-frame write-back (RSMods' Extended Range keeps its mode)
}
