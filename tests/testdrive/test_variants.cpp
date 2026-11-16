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

TEST(variant_colour_mode_per_frame) {
    Rs7Variant v;

    CHECK(Rs7Variant_ColourMode(v, 0) == 0);
    CHECK(Rs7Variant_ColourMode(v, 1) == 1);
    v.colourblind = true;
    CHECK(Rs7Variant_ColourMode(v, 0) == 1);
    CHECK(Rs7Variant_ColourMode(v, 1) == 1);
}

TEST(variant_force_righty_clears_a_left_handed_profile) {
    Rs7Variant v;

    v.righty = true;
    CHECK(v.Any());
    const Rs7GuitarFlags out = Rs7Variant_Apply(v, Rs7GuitarFlags{1, 1, 1});

    CHECK(out.lefty == 0 && out.inlineHead == 1 && out.colourblind == 1);
}

TEST(variant_parse_force) {
    CHECK(Rs7Variant_ParseForce(L"", L"lh", L"rh") == 0 && Rs7Variant_ParseForce(L"0", L"lh", L"rh") == 0);
    CHECK(Rs7Variant_ParseForce(nullptr, L"lh", L"rh") == 0);
    CHECK(Rs7Variant_ParseForce(L"1", L"lh", L"rh") == 1 && Rs7Variant_ParseForce(L"LH", L"lh", L"rh") == 1);
    CHECK(Rs7Variant_ParseForce(L"-1", L"lh", L"rh") == -1 && Rs7Variant_ParseForce(L"rh", L"lh", L"rh") == -1);
    CHECK(Rs7Variant_ParseForce(L"2", L"lh", L"rh") == -2 && Rs7Variant_ParseForce(L"left", L"lh", L"rh") == -2);
    CHECK(Rs7Variant_ParseForce(L"off", L"on", L"off") == -1 && Rs7Variant_ParseForce(L"rh", L"on", L"off") == -2);
}

TEST(variant_force_normal_colours) {
    Rs7Variant v;

    v.normalColours = true;
    CHECK(v.Any());
    CHECK(Rs7Variant_Apply(v, Rs7GuitarFlags{1, 0, 1}).colourblind == 0);
    CHECK(!std::strcmp(Rs7Variant_ColourModeText(v, "ColorBlindMild"), "ColorBlindNone"));
    CHECK(Rs7Variant_ColourMode(v, 1) == 1);  // no per-frame write-back (RSMods' Extended Range keeps its mode)
}
