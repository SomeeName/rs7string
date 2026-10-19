#include "model/status.h"
#include "test.h"

TEST(status_song_restart_and_profile_switch_replace_previous_tuning) {
    const auto before = Rs7Status_Song();
    const int16_t seven[7] = {-2, -2, -2, -2, -2, -2, -4};

    Rs7Status_SetSong(7, false, true, seven);
    auto s = Rs7Status_Song();

    CHECK(s.songs == before.songs + 1 && s.entries == 7 && s.active && !s.bass && s.t6 == -4);
    CHECK(s.tuning[6] == -4 && s.tuning[7] == 0);
    const int16_t bass[4] = {-1, 0, 1, 2};

    Rs7Status_SetSong(4, true, false, bass);
    s = Rs7Status_Song();
    CHECK(s.songs == before.songs + 2 && s.entries == 4 && s.bass && !s.active && s.t6 == 0);
    CHECK(s.tuning[0] == -1 && s.tuning[3] == 2);

    for (int i = 4; i < 8; ++i) {
        CHECK_EQ(s.tuning[i], 0);
    }
}

TEST(status_empty_song_and_missing_tuning_clear_all_previous_strings) {
    const int16_t eight[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    Rs7Status_SetSong(8, false, true, eight);
    CHECK_EQ(Rs7Status_Song().tuning[7], 8);
    Rs7Status_SetSong(0, false, false, nullptr);
    const auto s = Rs7Status_Song();

    CHECK(s.entries == 0 && !s.active && s.t6 == 0);

    for (int v : s.tuning) {
        CHECK_EQ(v, 0);
    }
}
