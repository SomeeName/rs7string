#include "features/pitch/pitch.h"
#include "test.h"
#include <cstring>
#include <limits>

TEST(pitch_empty_and_partial_tuning_clear_previous_extended_offsets) {
    int16_t out[8];

    std::memset(out, 0x55, sizeof out);
    rs7p::FillTuning(nullptr, 0, out);

    for (int v : out) {
        CHECK_EQ(v, 0);
    }

    const int16_t bass[4] = {-2, 0, 1, -5};

    rs7p::FillTuning(bass, 4, out);
    CHECK(out[0] == -2 && out[1] == 0 && out[2] == 1 && out[3] == -5);

    for (int s = 4; s < 8; ++s) {
        CHECK_EQ(out[s], 0);
    }
}

TEST(pitch_lowest_limits_extra_strings_to_the_active_width_and_instrument) {
    const int16_t tuning[8] = {0, 0, 0, 0, 0, 0, -7, -12};

    // hooks.cpp: g_rs7_pitch_gbase[6/7] = 35/30, so lowered extras are 35-7=28 and 30-12=18.
    // g_rs7_pitch_bbase[0] = 28; bass keeps its stock lowest value and ignores guitar extras.
    CHECK_EQ(rs7p::Lowest(40, tuning, 0, false), 40);
    CHECK_EQ(rs7p::Lowest(40, tuning, 6, false), 40);
    CHECK_EQ(rs7p::Lowest(40, tuning, 7, false), 28);
    CHECK_EQ(rs7p::Lowest(40, tuning, 8, false), 18);
    CHECK_EQ(rs7p::Lowest(40, tuning, (std::numeric_limits<size_t>::max)(), false), 18);
    CHECK_EQ(rs7p::Lowest(28, tuning, 8, true), 28);
    CHECK_EQ(rs7p::Lowest(12, tuning, 8, false), 12);
}

TEST(pitch_expected_pitch_respects_bass_bounds_and_unsigned_fallback) {
    const int16_t tuning[8] = {-12, 0, 0, 0, 0, 0, -2, -5};

    // hooks.cpp base tables: guitar[6/7] = 35/30, bass[0/3] = 28/43.
    // Expected pitches: 35-2+0=33, 30-5+24=49, 28-12+0=16, 43+0+24=67.
    CHECK_EQ(rs7p::ExpectedPitch8(tuning, 0, 6, 0), 33u);
    CHECK_EQ(rs7p::ExpectedPitch8(tuning, 0, 7, 24), 49u);
    CHECK_EQ(rs7p::ExpectedPitch8(tuning, 2, 0, 0), 16u);
    CHECK_EQ(rs7p::ExpectedPitch8(tuning, 2, 3, 24), 67u);

    for (uint32_t string : {4u, 6u, 7u, UINT32_MAX}) {
        CHECK_EQ(rs7p::ExpectedPitch8(tuning, 2, string, 0), UINT32_MAX);
        CHECK_EQ(rs7p::ExpectedPitch8(tuning, 2, string, 24), 23u);
    }

    CHECK_EQ(rs7p::ExpectedPitch8(tuning, 0, 8, 0), UINT32_MAX);
}
