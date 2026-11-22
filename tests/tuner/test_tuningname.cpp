#include "config_fixture.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuningname.h"
#include "tempfile.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using rsxrtn::Name7;

TEST(tuningname_active_calls_are_private_to_each_thread) {
    rsxrtest::Restore restore_names(g_rsxr_tn_on);
    uint8_t out[0x18] = {};
    uint32_t handles[rsxrtuner::kNameCalls] = {};
    int opened = 0;

    g_rsxr_tn_on = 1;
    const uint32_t outer = Rsxr_TnUuidPre(out);

    CHECK(outer != 0);
    std::thread worker([&]() {
        for (auto& h : handles) {
            h = Rsxr_TnUuidPre(out);
            opened += h != 0;
        }

        for (int i = rsxrtuner::kNameCalls - 1; i >= 0; --i) {
            Rsxr_TnUuidPost(handles[i]);
        }
    });

    worker.join();
    CHECK_EQ(opened, rsxrtuner::kNameCalls);
    Rsxr_TnUuidPost(outer);
}

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
        for (int d : {-6, -1, 1, 2, 5, -12, 12}) {  // only k, k - 2 and k - 3 .. k - 5 are names
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
    CHECK_EQ(rsxrtn::Compose("B Standard", "", "7Str"), std::string("B Standard 7Str"));
    CHECK_EQ(rsxrtn::Compose("Drop A", "7Str", ""), std::string("7Str Drop A"));
    CHECK_EQ(rsxrtn::Compose("Drop A", "", ""), std::string("Drop A"));
    CHECK_EQ(rsxrtn::Compose("Drop A", nullptr, nullptr), std::string("Drop A"));
    CHECK_EQ(rsxrtn::Compose("Bb Drop Ab", "7", "Str"), std::string("7 Bb Drop Ab Str"));
}

TEST(tuningname_display_is_the_game_style) {
    CHECK_EQ(rsxrtn::Display("B Standard 7Str"), std::string("B STANDARD 7STR"));
    CHECK_EQ(rsxrtn::Display("Drop A 7Str"), std::string("DROP A 7STR"));
    CHECK_EQ(rsxrtn::Display("Bb Drop Ab 7Str"), std::string("B\xE2\x99\xAD DROP A\xE2\x99\xAD 7STR"));
    CHECK_EQ(rsxrtn::Display("Eb Standard"), std::string("E\xE2\x99\xAD STANDARD"));           // = maingame.csv 36578
    CHECK_EQ(rsxrtn::Display("Eb Drop Db"), std::string("E\xE2\x99\xAD DROP D\xE2\x99\xAD"));  // = 36579
    CHECK_EQ(rsxrtn::Display("C# Standard"), std::string("C\xE2\x99\xAF STANDARD"));           // = 36580
    CHECK_EQ(rsxrtn::Display("Extended Drop B"), std::string("EXTENDED DROP B"));
    CHECK_EQ(rsxrtn::Display("Open G7"), std::string("OPEN G7"));
    CHECK_EQ(rsxrtn::Display("Abc bb #"), std::string("ABC BB #"));  // only note + accidental words
    CHECK_EQ(rsxrtn::Display(""), std::string(""));
}

TEST(tuningname_reference_pitch_like_the_game) {
    CHECK_EQ(rsxrtn::ReferenceA(0), 440);
    CHECK_EQ(rsxrtn::ReferenceA(-14), 436);
    CHECK_EQ(rsxrtn::ReferenceA(10), 442);
    CHECK_EQ(rsxrtn::ReferenceA(-100), 415);
    CHECK_EQ(rsxrtn::ReferenceA(100), 466);
    CHECK_EQ(rsxrtn::ReferenceA(-1), 439);  // truncation, not rounding
    CHECK_EQ(rsxrtn::ReferenceA(1), 440);
    CHECK_EQ(rsxrtn::Decoration(0), std::string(""));
    CHECK_EQ(rsxrtn::Decoration(-14), std::string(": A436"));
}

TEST(tuningname_known_display_with_decoration) {
    rsxrtn::Config c;
    const int b[7] = {0, 0, 0, 0, 0, 0, 0};
    const int bbDrop[7] = {-1, -1, -1, -1, -1, -1, -3};
    const int custom[7] = {0, 0, 0, 0, 0, 0, -6}, dropFs[7] = {0, 0, 0, 0, 0, 0, -5};

    CHECK_EQ(rsxrtn::KnownDisplay(b, c, 0, true), std::string("B STANDARD 7STR"));
    CHECK_EQ(rsxrtn::KnownDisplay(b, c, -14, true), std::string("B STANDARD 7STR: A436"));
    CHECK_EQ(rsxrtn::KnownDisplay(b, c, -14, false), std::string("B STANDARD 7STR"));
    CHECK_EQ(rsxrtn::KnownDisplay(bbDrop, c, 0, true), std::string("B\xE2\x99\xAD DROP A\xE2\x99\xAD 7STR"));
    CHECK_EQ(rsxrtn::KnownDisplay(custom, c, 0, true), std::string(""));
    CHECK_EQ(rsxrtn::KnownDisplay(dropFs, c, 0, true), std::string("DROP F\xE2\x99\xAF 7STR"));
    strcpy_s(c.prefix, "7Str");
    c.suffix[0] = 0;
    CHECK_EQ(rsxrtn::KnownDisplay(b, c, 0, true), std::string("7STR B STANDARD"));
}

TEST(tuningname_mark_stock_keeps_the_decoration_last) {
    CHECK_EQ(rsxrtn::MarkStock("E STANDARD: A436", "", "7Str"), std::string("E STANDARD 7STR: A436"));
    CHECK_EQ(rsxrtn::MarkStock("CUSTOM TUNING", "", "7Str"), std::string("CUSTOM TUNING 7STR"));
    CHECK_EQ(rsxrtn::MarkStock("OPEN G", "7Str", ""), std::string("7STR OPEN G"));
    CHECK_EQ(rsxrtn::MarkStock("X: Abc", "", "7Str"), std::string("X: Abc 7STR"));  // not a decoration
    CHECK_EQ(rsxrtn::MarkStock("X: A", "", "7Str"), std::string("X: A 7STR"));
    CHECK_EQ(rsxrtn::MarkStock("E STANDARD", "", ""), std::string("E STANDARD"));
}

TEST(tuningname_config_defaults_and_overrides) {
    const RsxrTestConfig d = RsxrTest_LoadConfig(L"Z:\\does\\not\\exist\\rsxr.ini");

    CHECK(d.tunerNames);
    CHECK_EQ(d.nameSuffix, std::string("7Str"));
    CHECK_EQ(d.namePrefix, std::string(""));

    std::wstring p = rsxrtest::TempIni(L"tuningname", "[tuner]\r\nnames=0\r\nname_prefix=7Str\r\nname_suffix=\r\n");
    RsxrTestConfig c = RsxrTest_LoadConfig(p.c_str());

    CHECK(!c.tunerNames);
    CHECK_EQ(c.namePrefix, std::string("7Str"));
    CHECK_EQ(c.nameSuffix, std::string(""));  // present but empty: no suffix
    CHECK(c.nameAffixValid);

    p = rsxrtest::TempIni(L"tuningname", "[tuner]\r\nname_suffix=0123456789012345678901234567890123\r\n");  // 34 > 31
    c = RsxrTest_LoadConfig(p.c_str());
    CHECK_EQ(c.nameSuffix, std::string("7Str"));
    CHECK(!c.nameAffixValid);

    p = rsxrtest::TempIni(L"tuningname", "[tuner]\r\nname_suffix=(7)\r\n");
    c = RsxrTest_LoadConfig(p.c_str());
    CHECK_EQ(c.nameSuffix, std::string("(7)"));
    DeleteFileW(p.c_str());

    CHECK(Rsxr_ValidNameAffix(L"7Str"));
    CHECK(Rsxr_ValidNameAffix(L""));
    CHECK(!Rsxr_ValidNameAffix(L"7\x00E4"));
    CHECK(!Rsxr_ValidNameAffix(L"a\tb"));
}

TEST(tuningname_stubs_off_without_the_switch) {
    // With g_rsxr_tn_on == 0 the C entry points do nothing (the stubs do not even call them).
    rsxrtest::Restore restore_g_rsxr_tn_on(g_rsxr_tn_on);

    g_rsxr_tn_on = 0;
    int offs[6] = {};
    uint32_t post = 7;

    CHECK_EQ(Rsxr_TnJsonPre(nullptr, nullptr, nullptr, 0), 0u);
    CHECK_EQ(Rsxr_TnUuidPre(offs), 0u);
    CHECK_EQ(Rsxr_TnArrayPre(offs, offs, &post), 0u);
    CHECK_EQ(post, 0u);
    g_rsxr_tn_on = 1;
    CHECK_EQ(Rsxr_TnArrayPre(offs, offs, &post), 0u);  // no TK key for this array: stock
    CHECK_EQ(post, 0u);
}

TEST(tuningname_eight_standard_drop_and_unknown) {
    const int fs[8] = {}, e[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
    const int drop[8] = {0, 0, 0, 0, 0, 0, 0, -2};

    CHECK(Is(rsxrtn::Name8(fs), "F# Standard"));
    CHECK(Is(rsxrtn::Name8(e), "E Standard"));
    CHECK(Is(rsxrtn::Name8(drop), "Drop E"));

    for (int i = 0; i < 8; ++i) {
        int t[8] = {};

        t[i] = 1;
        CHECK(rsxrtn::Name8(t) == nullptr);
    }

    rsxrtn::Config c;  // [tuner] names alone gates 8-string names (RsxrString7 marks the chart)

    CHECK(rsxrtn::KnownDisplay8(fs, c, 0, false) == "F\xE2\x99\xAF STANDARD 8STR");
    CHECK(rsxrtn::KnownDisplay8(e, c, -16, true) == "E STANDARD 8STR: A435");
    c.on = false;
    CHECK(rsxrtn::KnownDisplay8(fs, c, 0, false).empty());
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

        CHECK(Is(rsxrtn::Name8(t), names[-k]));
        t[7] -= 2;
        CHECK(Is(rsxrtn::Name8(t), drops[-k]));
        t[7] += 12;
        CHECK(rsxrtn::Name8(t) == nullptr);
    }

    const int fs[8] = {};
    rsxrtn::Config c;

    c.on = false;
    CHECK(rsxrtn::KnownDisplay8(fs, c, 0, false).empty());
    c.on = true;
    strcpy_s(c.prefix, "Demo");
    strcpy_s(c.suffix, "custom7");
    CHECK(rsxrtn::KnownDisplay8(fs, c, 0, false) == "DEMO F\xE2\x99\xAF STANDARD CUSTOM8");
    strcpy_s(c.prefix, "");
    strcpy_s(c.suffix, "ext");
    CHECK(rsxrtn::KnownDisplay8(fs, c, 0, false) == "F\xE2\x99\xAF STANDARD 8STR");
}

TEST(tuningname_eight_suffix_from_seven) {
    CHECK(rsxrtn::SuffixN("7Str", 8) == "8Str");
    CHECK(rsxrtn::SuffixN("7-String", 8) == "8-String");
    CHECK(rsxrtn::SuffixN("", 8) == "8Str");
    CHECK(rsxrtn::SuffixN(nullptr, 8) == "8Str");
    CHECK(rsxrtn::SuffixN("ext", 8) == "8Str");
}

TEST(tuningname_extended_bass_standard_is_named_by_its_low_b) {
    const int beadg[6] = {0, 0, 0, 0, 0, 0}, down[6] = {-1, -1, -1, -1, -1, 0}, drop[6] = {-2, 0, 0, 0, 0, 0};
    const rsxrtn::Config c;

    CHECK(std::string(rsxrtn::NameBass(beadg, 5)) == "B Standard");
    CHECK(std::string(rsxrtn::NameBass(beadg, 6)) == "B Standard");
    CHECK(std::string(rsxrtn::NameBass(down, 5)) == "Bb Standard");  // [5] is not played on 5 strings
    CHECK(rsxrtn::NameBass(down, 6) == nullptr);
    CHECK(rsxrtn::NameBass(drop, 5) == nullptr);
    CHECK(rsxrtn::NameBass(beadg, 4) == nullptr);
    CHECK(rsxrtn::KnownDisplayBass(beadg, 5, c, 0, false) == "B STANDARD 5STR");
    CHECK(rsxrtn::KnownDisplayBass(beadg, 6, c, 0, false) == "B STANDARD 6STR");
    CHECK(rsxrtn::SuffixN("7-String", 5) == "5-String");
    CHECK(rsxrtn::SuffixN("", 6) == "6Str");
}

TEST(tuningname_extended_bass_drops_name_the_low_b) {
    const int dropA[6] = {0, 0, 0, 0, -2, 0}, dropG[6] = {0, 0, 0, 0, -4, 0};
    const int aDropFs[6] = {-2, -2, -2, -2, -5, -2}, tooLow[6] = {0, 0, 0, 0, -6, 0}, up[6] = {0, 0, 0, 0, 1, 0};
    const rsxrtn::Config c;

    CHECK(std::string(rsxrtn::NameBass(dropA, 5)) == "Drop A");
    CHECK(std::string(rsxrtn::NameBass(dropA, 6)) == "Drop A");
    CHECK(std::string(rsxrtn::NameBass(dropG, 5)) == "Drop G");
    CHECK(std::string(rsxrtn::NameBass(aDropFs, 6)) == "A Drop F#");
    CHECK(rsxrtn::NameBass(tooLow, 5) == nullptr);  // 6 below: no name
    CHECK(rsxrtn::NameBass(up, 5) == nullptr);      // the low B above the others
    CHECK(rsxrtn::KnownDisplayBass(dropA, 5, c, 0, false) == "DROP A 5STR");
    CHECK(rsxrtn::KnownDisplayBass(aDropFs, 6, c, 0, false) == "A DROP F\xE2\x99\xAF 6STR");
}

TEST(tuningname_deep_drops_three_to_five_below_the_family) {
    int t7[7] = {0, 0, 0, 0, 0, 0, 0};
    const char* seven[] = {"Drop Ab", "Drop G", "Drop F#"};

    for (int d = 3; d <= 5; ++d) {
        t7[6] = -d;
        CHECK(Is(Name7(t7), seven[d - 3]));
    }

    t7[6] = -1;
    CHECK(Name7(t7) == nullptr);
    t7[6] = -6;
    CHECK(Name7(t7) == nullptr);

    const int aDropF[7] = {-2, -2, -2, -2, -2, -2, -6};

    CHECK(Is(Name7(aDropF), "A Drop F"));

    const int dropD8[8] = {0, 0, 0, 0, 0, 0, 0, -4}, dropDb8[8] = {0, 0, 0, 0, 0, 0, 0, -5};
    const int dDropBb8[8] = {-4, -4, -4, -4, -4, -4, -4, -8}, dDropC8[8] = {-4, -4, -4, -4, -4, -4, -4, -6};
    const rsxrtn::Config c;

    CHECK(Is(rsxrtn::Name8(dropD8), "Drop D"));
    CHECK(Is(rsxrtn::Name8(dropDb8), "Drop Db"));
    CHECK(Is(rsxrtn::Name8(dDropBb8), "D Drop Bb"));
    CHECK(Is(rsxrtn::Name8(dDropC8), "D Drop C"));  // the 8-string "Drop C1" (C G C F Bb Eb G C)
    CHECK(rsxrtn::KnownDisplay8(dropDb8, c, 0, false) == "DROP D\xE2\x99\xAD 8STR");
}
