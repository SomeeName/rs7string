#include "features/publish/publish.h"
#include "test.h"

TEST(song_width_accepts_only_supported_guitar_widths) {
    CHECK_EQ(rs7pubstate::Count(6, false), 6u);
    CHECK_EQ(rs7pubstate::Count(7, false), 7u);
    CHECK_EQ(rs7pubstate::Count(8, false), 8u);

    for (size_t n : {size_t(0), size_t(4), size_t(5), size_t(9), size_t(16)}) {
        CHECK_EQ(rs7pubstate::Count(n, false), 6u);
    }

    for (size_t n : {size_t(4), size_t(6), size_t(7), size_t(8)}) {
        CHECK_EQ(rs7pubstate::Count(n, true), 4u);
    }
}

TEST(song_lowest_uses_all_active_strings) {
    const int16_t t[8] = {0, -20, 0, 0, 0, 0, -12, -12};

    CHECK_EQ(rs7pubstate::Lowest(t, 6, false), 25);
    CHECK_EQ(rs7pubstate::Lowest(t, 7, false), 23);
    CHECK_EQ(rs7pubstate::Lowest(t, 8, false), 18);
    CHECK_EQ(rs7pubstate::Lowest(t, 4, true), 13);
}

TEST(song_lowest_over_six_strings_is_the_stock_filter) {
    const int16_t t[8] = {-2, 0, 0, 0, 0, 0, -2, -2};

    CHECK_EQ(rs7pubstate::Lowest(t, 8, false), 28);
    CHECK_EQ(rs7pubstate::Lowest(t, 6, false), 38);
}
