#include "features/tuner/tuner.h"
#include "test.h"
#include <cstring>
#include <limits>

TEST(tuner_navigation_walks_each_active_string_once_in_both_directions) {
    const uint8_t expected[3][8] = {{0, 1, 2, 3, 4, 5, 6, 7}, {6, 0, 1, 2, 3, 4, 5, 7}, {7, 6, 0, 1, 2, 3, 4, 5}};

    for (int n : {4, 6, 7, 8}) {
        uint8_t order[8];

        rs7tu::Order(n, order);
        const uint8_t* want = expected[n <= 6 ? 0 : n - 6];

        for (int i = 0; i < n; ++i) {
            CHECK_EQ(order[i], want[i]);
            CHECK_EQ(rs7tu::Step(order, n, want[i], 1), i + 1 < n ? want[i + 1] : -1);
            CHECK_EQ(rs7tu::Step(order, n, want[i], -1), i ? want[i - 1] : -1);
        }
    }
}

TEST(tuner_navigation_rejects_empty_corrupt_widths_and_missing_current_string) {
    const uint8_t order[8] = {6, 0, 1, 2, 3, 4, 5, 7};

    for (int n : {-1, 0, 9, (std::numeric_limits<int>::max)()}) {
        CHECK_EQ(rs7tu::Step(nullptr, n, 0, 1), -1);
    }

    CHECK_EQ(rs7tu::Step(order, 7, 7, 1), -1);
    CHECK_EQ(rs7tu::Step(order, 7, -1, -1), -1);
    CHECK_EQ(rs7tu::Step(order, 7, 255, 1), -1);
}

TEST(tuner_note_names_normalise_negative_octaves_and_integer_extremes) {
    CHECK(std::strcmp(rs7tu::NoteName(-12), "C") == 0);
    CHECK(std::strcmp(rs7tu::NoteName(-13), "B") == 0);
    CHECK(std::strcmp(rs7tu::NoteName((std::numeric_limits<int>::min)()), "E") == 0);
    CHECK(std::strcmp(rs7tu::NoteName((std::numeric_limits<int>::max)()), "G") == 0);
    CHECK(std::strcmp(rs7tu::NoteName(127), "G") == 0);
}

TEST(tuner_targets_keep_independent_drop_offsets_for_both_extended_strings) {
    const int stock[6] = {-2, 0, 0, 0, 0, 0};
    const int extra[2] = {-2, -5};
    int open[8], offsets[8];

    rs7tu::Targets(stock, extra, open, offsets);
    const int want[8] = {38, 45, 50, 55, 59, 64, 33, 25};

    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(open[i], want[i]);
    }

    CHECK_EQ(offsets[0], -2);
    CHECK_EQ(offsets[6], -2);
    CHECK_EQ(offsets[7], -5);
}
