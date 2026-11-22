#include "model/status.h"
#include "test.h"
#include <cstring>

TEST(status_song_restart_and_profile_switch_replace_previous_tuning) {
    const auto before = RsxrStatus_Song();
    const int16_t seven[7] = {-2, -2, -2, -2, -2, -2, -4};

    RsxrStatus_SetSong(7, 7, false, true, seven);
    auto s = RsxrStatus_Song();

    CHECK(s.songs == before.songs + 1 && s.entries == 7 && s.active && !s.bass && s.t6 == -4);
    CHECK(s.tuning[6] == -4 && s.tuning[7] == 0);
    const int16_t bass[4] = {-1, 0, 1, 2};

    RsxrStatus_SetSong(4, 4, true, false, bass);
    s = RsxrStatus_Song();
    CHECK(s.songs == before.songs + 2 && s.entries == 4 && s.bass && !s.active && s.t6 == 0);
    CHECK(s.tuning[0] == -1 && s.tuning[3] == 2);

    for (int i = 4; i < 8; ++i) {
        CHECK_EQ(s.tuning[i], 0);
    }
}

TEST(status_empty_song_and_missing_tuning_clear_all_previous_strings) {
    const int16_t eight[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    RsxrStatus_SetSong(8, 8, false, false, eight);
    CHECK_EQ(RsxrStatus_Song().tuning[7], 8);
    RsxrStatus_SetSong(0, 6, false, false, nullptr);
    const auto s = RsxrStatus_Song();

    CHECK(s.entries == 0 && !s.active && s.t6 == 0);

    for (int v : s.tuning) {
        CHECK_EQ(v, 0);
    }
}

TEST(status_song_rows_show_the_real_string_count) {
    RsxrStatusRow rows[6];
    RsxrSongStatus s;

    CHECK_EQ(RsxrStatus_SongRows(s, rows, 6), 1);  // no song yet
    s.songs = 1;
    s.entries = 8;
    s.strings = 8;
    CHECK_EQ(RsxrStatus_SongRows(s, rows, 6), 4);
    CHECK(rows[0].on && !std::strcmp(rows[0].text, "ON"));
    CHECK(!std::strcmp(rows[1].text, "8 (8 SNG tuning entries)"));
    s.bass = true;
    s.entries = 6;
    s.strings = 5;
    RsxrStatus_SongRows(s, rows, 6);
    CHECK(rows[0].on && !std::strcmp(rows[1].text, "5 (6 SNG tuning entries, bass)"));
    s.bass = false;
    s.strings = 6;
    RsxrStatus_SongRows(s, rows, 6);
    CHECK(!rows[0].on && !std::strcmp(rows[0].text, "off"));
    CHECK_EQ(RsxrStatus_SongRows(s, rows, 2), 2);  // never more than max
}
