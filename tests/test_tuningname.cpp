#include "core/config.h"
#include "core/tuningname.h"
#include "test.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

using rs7tn::Name7;

namespace {
// t = strings 0..5 all k, the 7th string t6.
const char* Uniform(int k, int t6) {
    const int t[7] = {k, k, k, k, k, k, t6};

    return Name7(t);
}

bool Is(const char* got, const char* want) {
    return got && want && std::strcmp(got, want) == 0;
}

struct Row {
    int k;
    const char* standard;
    const char* drop;
};

// The owner's list (issue #15, 2026-10-13): every 7-string standard and its single-low-string drop.
const Row kOwnerList[] = {
    {+1, "C Standard", "C Drop Bb"},   {0, "B Standard", "Drop A"},       {-1, "Bb Standard", "Bb Drop Ab"},
    {-2, "A Standard", "A Drop G"},    {-3, "Ab Standard", "Ab Drop Gb"}, {-4, "G Standard", "G Drop F"},
    {-5, "Gb Standard", "Gb Drop E"},  {-6, "F Standard", "F Drop Eb"},   {-7, "E Standard", "E Drop D"},
    {-8, "Eb Standard", "Eb Drop Db"}, {-9, "D Standard", "D Drop C"},
};

std::wstring TempIni(const char* content) {
    wchar_t dir[MAX_PATH];

    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"rs7string_test_tuningname.ini";
    FILE* f = nullptr;

    _wfopen_s(&f, path.c_str(), L"wb");

    if (f) {
        std::fputs(content, f);
        std::fclose(f);
    }

    return path;
}
}  // namespace

TEST(tuningname_b_standard_and_drop_a) {
    CHECK(Is(Uniform(0, 0), "B Standard"));
    CHECK(Is(Uniform(0, -2), "Drop A"));
}

TEST(tuningname_every_owner_list_entry) {
    for (const Row& r : kOwnerList) {
        CHECK(Is(Uniform(r.k, r.k), r.standard));
        CHECK(Is(Uniform(r.k, r.k - 2), r.drop));
    }
}

TEST(tuningname_owner_note_lists) {
    // Offsets from B1 E2 A2 D3 G3 B3 E4. C Standard = C F Bb Eb Ab C F (all +1), B Standard =
    // B E A D G B E, E Standard = E A D G C E A (all -7).
    CHECK(Is(Uniform(1, 1), "C Standard"));
    CHECK(Is(Uniform(-7, -7), "E Standard"));
    CHECK(Is(Uniform(-7, -9), "E Drop D"));  // not "Drop D": that is the 6-string name
}

TEST(tuningname_alternates_by_exact_offsets) {
    const int extD[7] = {-2, 0, 0, 0, 0, 0, -2};       // A D A D G B E
    const int extC[7] = {-4, -2, -2, -2, -2, -2, -4};  // G C G C F A D
    const int extB[7] = {-5, -3, -3, -3, -3, -3, -5};  // F# B F# B E G# C#
    const int ddd[7] = {5, 5, 5, 4, 3, -2, 3};         // D A D G B D D (registers: HYPOTHESIS)
    const int og7[7] = {-2, -2, -3, -5, -6, -9, -4};   // G D G B D F G (registers: HYPOTHESIS)

    CHECK(Is(Name7(extD), "Extended Drop D"));
    CHECK(Is(Name7(extC), "Extended Drop C"));
    CHECK(Is(Name7(extB), "Extended Drop B"));
    CHECK(Is(Name7(ddd), "Double Drop D"));
    CHECK(Is(Name7(og7), "Open G7"));
}

TEST(tuningname_alternate_near_misses_are_unknown) {
    struct Alt {
        int t[7];
        const char* name;
    };

    const Alt alts[] = {{{-2, 0, 0, 0, 0, 0, -2}, "Extended Drop D"},
                        {{-4, -2, -2, -2, -2, -2, -4}, "Extended Drop C"},
                        {{-5, -3, -3, -3, -3, -3, -5}, "Extended Drop B"},
                        {{5, 5, 5, 4, 3, -2, 3}, "Double Drop D"},
                        {{-2, -2, -3, -5, -6, -9, -4}, "Open G7"}};

    for (const Alt& a : alts) {
        for (int s = 0; s < 7; ++s) {
            for (int d : {-1, 1, 12, -12}) {  // one string a semitone or an octave off: no octave folding
                int t[7];

                std::memcpy(t, a.t, sizeof t);
                t[s] += d;
                const char* n = Name7(t);

                CHECK(!Is(n, a.name));

                for (const Alt& b : alts) {
                    CHECK(!Is(n, b.name));  // never another alternate either
                }
            }
        }
    }
}

TEST(tuningname_unknown_7th_string_offsets) {
    for (const Row& r : kOwnerList) {
        for (int d : {-3, -1, 1, 2, 5, -12, 12}) {  // only k and k - 2 are names
            CHECK(Uniform(r.k, r.k + d) == nullptr);
        }
    }
}

TEST(tuningname_outside_the_owner_range_is_unknown) {
    for (int k : {2, 3, 12, -10, -11, -12, -24, 24}) {
        CHECK(Uniform(k, k) == nullptr);
        CHECK(Uniform(k, k - 2) == nullptr);
    }
}

TEST(tuningname_uneven_six_strings_are_unknown) {
    const int dropD[7] = {-2, 0, 0, 0, 0, 0, 0};  // 6-string Drop D + low B: no 7-string name
    const int openG[7] = {-2, -2, 0, 0, 0, -2, -2};
    const int oneUp[7] = {0, 0, 0, 0, 0, 1, 0};
    const int dadgad[7] = {-2, 0, 0, 0, -2, -2, -2};

    CHECK(Name7(dropD) == nullptr);
    CHECK(Name7(openG) == nullptr);
    CHECK(Name7(oneUp) == nullptr);
    CHECK(Name7(dadgad) == nullptr);

    for (int s = 0; s < 6; ++s) {  // any one of strings 0..5 off B Standard
        int t[7] = {0, 0, 0, 0, 0, 0, 0};

        t[s] = -1;
        CHECK(Name7(t) == nullptr);
        t[6] = -2;
        CHECK(Name7(t) == nullptr);
    }
}

TEST(tuningname_compose_prefix_and_suffix) {
    CHECK_EQ(rs7tn::Compose("B Standard", "", "7Str"), std::string("B Standard 7Str"));
    CHECK_EQ(rs7tn::Compose("Drop A", "7Str", ""), std::string("7Str Drop A"));
    CHECK_EQ(rs7tn::Compose("Drop A", "", ""), std::string("Drop A"));
    CHECK_EQ(rs7tn::Compose("Drop A", nullptr, nullptr), std::string("Drop A"));
    CHECK_EQ(rs7tn::Compose("Bb Drop Ab", "7", "Str"), std::string("7 Bb Drop Ab Str"));
}

TEST(tuningname_display_is_the_game_style) {
    CHECK_EQ(rs7tn::Display("B Standard 7Str"), std::string("B STANDARD 7STR"));
    CHECK_EQ(rs7tn::Display("Drop A 7Str"), std::string("DROP A 7STR"));
    CHECK_EQ(rs7tn::Display("Bb Drop Ab 7Str"), std::string("B\xE2\x99\xAD DROP A\xE2\x99\xAD 7STR"));
    CHECK_EQ(rs7tn::Display("Eb Standard"), std::string("E\xE2\x99\xAD STANDARD"));           // = maingame.csv 36578
    CHECK_EQ(rs7tn::Display("Eb Drop Db"), std::string("E\xE2\x99\xAD DROP D\xE2\x99\xAD"));  // = 36579
    CHECK_EQ(rs7tn::Display("C# Standard"), std::string("C\xE2\x99\xAF STANDARD"));           // = 36580
    CHECK_EQ(rs7tn::Display("Extended Drop B"), std::string("EXTENDED DROP B"));
    CHECK_EQ(rs7tn::Display("Open G7"), std::string("OPEN G7"));
    CHECK_EQ(rs7tn::Display("Abc bb #"), std::string("ABC BB #"));  // only note + accidental words
    CHECK_EQ(rs7tn::Display(""), std::string(""));
}

TEST(tuningname_reference_pitch_like_the_game) {
    CHECK_EQ(rs7tn::ReferenceA(0), 440);
    CHECK_EQ(rs7tn::ReferenceA(-14), 436);
    CHECK_EQ(rs7tn::ReferenceA(10), 442);
    CHECK_EQ(rs7tn::ReferenceA(-100), 415);
    CHECK_EQ(rs7tn::ReferenceA(100), 466);
    CHECK_EQ(rs7tn::ReferenceA(-1), 439);  // truncation, not rounding
    CHECK_EQ(rs7tn::ReferenceA(1), 440);
    CHECK_EQ(rs7tn::Decoration(0), std::string(""));
    CHECK_EQ(rs7tn::Decoration(-14), std::string(": A436"));
}

TEST(tuningname_known_display_with_decoration) {
    rs7tn::Config c;
    const int b[7] = {0, 0, 0, 0, 0, 0, 0};
    const int bbDrop[7] = {-1, -1, -1, -1, -1, -1, -3};
    const int custom[7] = {0, 0, 0, 0, 0, 0, -5};

    CHECK_EQ(rs7tn::KnownDisplay(b, c, 0, true), std::string("B STANDARD 7STR"));
    CHECK_EQ(rs7tn::KnownDisplay(b, c, -14, true), std::string("B STANDARD 7STR: A436"));
    CHECK_EQ(rs7tn::KnownDisplay(b, c, -14, false), std::string("B STANDARD 7STR"));
    CHECK_EQ(rs7tn::KnownDisplay(bbDrop, c, 0, true), std::string("B\xE2\x99\xAD DROP A\xE2\x99\xAD 7STR"));
    CHECK_EQ(rs7tn::KnownDisplay(custom, c, 0, true), std::string(""));
    std::strcpy(c.prefix, "7Str");
    c.suffix[0] = 0;
    CHECK_EQ(rs7tn::KnownDisplay(b, c, 0, true), std::string("7STR B STANDARD"));
}

TEST(tuningname_mark_stock_keeps_the_decoration_last) {
    CHECK_EQ(rs7tn::MarkStock("E STANDARD: A436", "", "7Str"), std::string("E STANDARD 7STR: A436"));
    CHECK_EQ(rs7tn::MarkStock("CUSTOM TUNING", "", "7Str"), std::string("CUSTOM TUNING 7STR"));
    CHECK_EQ(rs7tn::MarkStock("OPEN G", "7Str", ""), std::string("7STR OPEN G"));
    CHECK_EQ(rs7tn::MarkStock("X: Abc", "", "7Str"), std::string("X: Abc 7STR"));  // not a decoration
    CHECK_EQ(rs7tn::MarkStock("X: A", "", "7Str"), std::string("X: A 7STR"));
    CHECK_EQ(rs7tn::MarkStock("E STANDARD", "", ""), std::string("E STANDARD"));
}

TEST(tuningname_config_defaults_and_overrides) {
    const Rs7Config d = Rs7Config_Load(L"Z:\\does\\not\\exist\\rs7string.ini");

    CHECK(d.tunerNames);
    CHECK_EQ(d.nameSuffix, std::string("7Str"));
    CHECK_EQ(d.namePrefix, std::string(""));

    std::wstring p = TempIni("[tuner]\r\nnames=0\r\nname_prefix=7Str\r\nname_suffix=\r\n");
    Rs7Config c = Rs7Config_Load(p.c_str());

    CHECK(!c.tunerNames);
    CHECK_EQ(c.namePrefix, std::string("7Str"));
    CHECK_EQ(c.nameSuffix, std::string(""));  // present but empty: no suffix
    CHECK(c.nameAffixValid);

    p = TempIni("[tuner]\r\nname_suffix=0123456789012345678901234567890123\r\n");  // 34 > 31
    c = Rs7Config_Load(p.c_str());
    CHECK_EQ(c.nameSuffix, std::string("7Str"));
    CHECK(!c.nameAffixValid);

    p = TempIni("[tuner]\r\nname_suffix=(7)\r\n");
    c = Rs7Config_Load(p.c_str());
    CHECK_EQ(c.nameSuffix, std::string("(7)"));
    DeleteFileW(p.c_str());

    CHECK(Rs7_ValidNameAffix(L"7Str"));
    CHECK(Rs7_ValidNameAffix(L""));
    CHECK(!Rs7_ValidNameAffix(L"7\x00E4"));
    CHECK(!Rs7_ValidNameAffix(L"a\tb"));
}

TEST(tuningname_stubs_off_without_the_switch) {
    // With g_rs7_tn_on == 0 the C entry points do nothing (the stubs do not even call them).
    const uintptr_t was = g_rs7_tn_on;

    g_rs7_tn_on = 0;
    int offs[6] = {};
    uint32_t post = 7;

    CHECK_EQ(Rs7_TnJsonPre(nullptr, nullptr, nullptr, 0), 0u);
    CHECK_EQ(Rs7_TnUuidPre(offs), 0u);
    CHECK_EQ(Rs7_TnArrayPre(offs, offs, &post), 0u);
    CHECK_EQ(post, 0u);
    g_rs7_tn_on = 1;
    CHECK_EQ(Rs7_TnArrayPre(offs, offs, &post), 0u);  // no TK key for this array: stock
    CHECK_EQ(post, 0u);
    g_rs7_tn_on = was;
}

TEST(tuningname_eight_standard_drop_and_unknown) {
    const int fs[8] = {}, e[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
    const int drop[8] = {0, 0, 0, 0, 0, 0, 0, -2};

    CHECK(Is(rs7tn::Name8(fs), "F# Standard"));
    CHECK(Is(rs7tn::Name8(e), "E Standard"));
    CHECK(Is(rs7tn::Name8(drop), "Drop E"));

    for (int i = 0; i < 8; ++i) {
        int t[8] = {};

        t[i] = 1;
        CHECK(rs7tn::Name8(t) == nullptr);
    }

    rs7tn::Config c;  // [tuner] names alone gates 8-string names (Rs7String7 marks the chart)

    CHECK(rs7tn::KnownDisplay8(fs, c, 0, false) == "F\xE2\x99\xAF STANDARD 8STR");
    CHECK(rs7tn::KnownDisplay8(e, c, -16, true) == "E STANDARD 8STR: A435");
    c.on = false;
    CHECK(rs7tn::KnownDisplay8(fs, c, 0, false).empty());
}

TEST(tuningname_eight_family_exact_registers_and_gates) {
    const char* names[] = {"F# Standard", "F Standard", "E Standard", "Eb Standard", "D Standard",
                           "Db Standard", "C Standard", "B Standard", "Bb Standard", "A Standard"};
    const char* drops[] = {"Drop E",    "F Drop Eb", "E Drop D", "Eb Drop Db", "D Drop C",
                           "Db Drop B", "C Drop Bb", "B Drop A", "Bb Drop Ab", "A Drop G"};

    for (int k = 0; k >= -9; --k) {
        int t[8];

        for (int& v : t) {
            v = k;
        }

        CHECK(Is(rs7tn::Name8(t), names[-k]));
        t[7] -= 2;
        CHECK(Is(rs7tn::Name8(t), drops[-k]));
        t[7] += 12;
        CHECK(rs7tn::Name8(t) == nullptr);
    }

    const int fs[8] = {};
    rs7tn::Config c;

    c.on = false;
    CHECK(rs7tn::KnownDisplay8(fs, c, 0, false).empty());
    c.on = true;
    std::strcpy(c.prefix, "Demo");
    std::strcpy(c.suffix, "custom7");
    CHECK(rs7tn::KnownDisplay8(fs, c, 0, false) == "DEMO F\xE2\x99\xAF STANDARD CUSTOM8");
    std::strcpy(c.prefix, "");
    std::strcpy(c.suffix, "ext");
    CHECK(rs7tn::KnownDisplay8(fs, c, 0, false) == "F\xE2\x99\xAF STANDARD 8STR");
}

TEST(tuningname_eight_suffix_from_seven) {
    CHECK(rs7tn::Suffix8("7Str") == "8Str");
    CHECK(rs7tn::Suffix8("7-String") == "8-String");
    CHECK(rs7tn::Suffix8("") == "8Str");
    CHECK(rs7tn::Suffix8(nullptr) == "8Str");
    CHECK(rs7tn::Suffix8("ext") == "8Str");
}
