#include "core/diagrams.h"
#include "core/nslot.h"
#include "test.h"
#include <cstring>
#include <map>
#include <string>

using Props = std::map<std::string, int>;

static void Collect(void* p, const char* key, int value) {
    (*static_cast<Props*>(p))[key] = value;
}

TEST(diagram_six_and_bass_emit_nothing) {
    rs7diagram::Shape s;
    Props p{{"LowEFret", 99}};

    for (int n : {4, 6}) {
        s.count = n;
        rs7diagram::Emit(s, Collect, &p);
    }

    CHECK_EQ(p.size(), 1u);
    CHECK_EQ(p["LowEFret"], 99);
}

TEST(diagram_seven_only_high_fret_moves_the_whole_window) {
    rs7diagram::Shape s;

    s.count = 7;
    s.frets[0] = 0;
    s.frets[6] = 12;
    s.fingers[6] = 0;
    Props p;

    rs7diagram::Emit(s, Collect, &p);
    CHECK_EQ(p["StartFret"], 12);
    CHECK_EQ(p["LowEFret"], 0);
    CHECK_EQ(p["LowBFret"], 1);
    CHECK_EQ(p["LowB_Finger"], 0);
    CHECK_EQ(p["LowB_Color"], 0x808080);
    CHECK(p.count("LowFsFret") == 0);
}

TEST(diagram_eight_muted_row_and_absent_fingers) {
    rs7diagram::Shape s;

    s.count = 8;
    s.frets[6] = 3;
    s.fingers[6] = 255;
    Props p;

    rs7diagram::Emit(s, Collect, &p);
    CHECK_EQ(p["StartFret"], 1);
    CHECK_EQ(p["LowBFret"], 3);
    CHECK_EQ(p["LowFsFret"], -1);
    CHECK_EQ(p["LowFs_Color"], 0x00ffff);
    CHECK(p.count("LowB_Finger") == 0);
    CHECK(p.count("LowFs_Finger") == 0);
}

TEST(diagram_manifest_capo_preserves_open_and_signed_mute) {
    rs7diagram::Shape s;

    s.count = 8;
    s.manifest = true;
    s.capo = 2;
    s.frets[0] = 2;
    s.frets[6] = 9;
    s.frets[7] = -1;
    s.fingers[6] = 4;
    Props p;

    rs7diagram::Emit(s, Collect, &p);
    CHECK_EQ(p["StartFret"], 9);
    CHECK_EQ(p["LowEFret"], 0);
    CHECK_EQ(p["LowBFret"], 1);
    CHECK_EQ(p["LowFsFret"], -1);
    CHECK_EQ(p["LowB_Finger"], 4);
}

TEST(diagram_t8_reads_independent_eight_slot_arrays) {
    uint8_t t[rs7ns::kT8Size] = {};

    t[rs7ns::kT8Slots] = 8;
    std::memset(t + rs7ns::kT8Frets, 255, 8);
    std::memset(t + rs7ns::kT8Fingers, 255, 8);
    t[rs7ns::kT8Frets + 7] = 14;
    t[rs7ns::kT8Fingers + 7] = 2;
    rs7diagram::Shape s;

    CHECK(rs7diagram::FromT8(t, &s));
    Props p;

    rs7diagram::Emit(s, Collect, &p);
    CHECK_EQ(p["StartFret"], 14);
    CHECK_EQ(p["LowFsFret"], 1);
    CHECK_EQ(p["LowFs_Finger"], 2);
    t[rs7ns::kT8Slots] = 6;
    CHECK(!rs7diagram::FromT8(t, &s));
}

TEST(diagram_manifest_rs7x_match_checks_six_columns_and_keeps_capo) {
    uint8_t t[rs7ns::kT8Size] = {};

    t[rs7ns::kT8Slots] = 7;
    std::memset(t + rs7ns::kT8Frets, 255, 8);
    std::memset(t + rs7ns::kT8Fingers, 255, 8);
    t[rs7ns::kT8Frets] = 0;
    t[rs7ns::kT8Frets + 6] = 12;
    t[rs7ns::kT8Fingers + 6] = 1;
    rs7diagram::Shape s;

    s.manifest = true;
    s.capo = 2;
    s.frets[0] = 2;
    CHECK(rs7diagram::MergeManifestT8(t, &s));
    CHECK_EQ(s.count, 7);
    Props p;

    rs7diagram::Emit(s, Collect, &p);
    CHECK_EQ(p["StartFret"], 12);
    CHECK_EQ(p["LowEFret"], 0);
    s.frets[0] = 3;
    CHECK(!rs7diagram::MergeManifestT8(t, &s));
}

TEST(diagram_eight_string_producer_preserves_both_rs7x_columns_when_enabled) {
    uint8_t stock[rs7ns::kTplSize] = {}, t8[rs7ns::kT8Size] = {};

    std::memset(stock + 4, 255, 12);
    const rs7ns::Col extra[2] = {{12, 1}, {14, 2}};
    const int16_t tuning[8] = {};
    const uintptr_t saved = g_rs7_diagrams;

    g_rs7_diagrams = 1;
    rs7ns::BuildT8(stock, extra, tuning, 8, 255, t8);
    g_rs7_diagrams = saved;
    rs7diagram::Shape shape;

    CHECK(rs7diagram::FromT8(t8, &shape));
    Props p;

    rs7diagram::Emit(shape, Collect, &p);
    CHECK_EQ(p["LowBFret"], 1);
    CHECK_EQ(p["LowFsFret"], 3);
    CHECK_EQ(p["LowFs_Finger"], 2);
}
