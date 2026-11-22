// rsxrmatch::OctaveDyad, LowChord, Compact6 (the target-set predicates of the note matcher).
#include "features/nslot/match.h"
#include "test.h"

using rsxrmatch::Compact6;
using rsxrmatch::LowChord;
using rsxrmatch::OctaveDyad;

TEST(match_octavedyad_lower_note_first) {
    const int32_t a[2] = {40, 52};
    const int32_t b[3] = {0, 40, 52};  // zeros are skipped
    const int32_t c[4] = {-1, 40, 0, 52};

    CHECK(OctaveDyad(a, 2));
    CHECK(OctaveDyad(b, 3));
    CHECK(OctaveDyad(c, 4));
}

TEST(match_octavedyad_rejects_other_shapes) {
    const int32_t high_first[2] = {52, 40};
    const int32_t fifth[2] = {40, 47};
    const int32_t same[2] = {40, 40};
    const int32_t single[2] = {40, 0};
    const int32_t none[2] = {0, 0};
    const int32_t three[3] = {40, 52, 64};
    const int32_t three_b[3] = {40, 52, 52};

    CHECK(!OctaveDyad(high_first, 2));
    CHECK(!OctaveDyad(fifth, 2));
    CHECK(!OctaveDyad(same, 2));
    CHECK(!OctaveDyad(single, 2));
    CHECK(!OctaveDyad(none, 2));
    CHECK(!OctaveDyad(three, 3));  // 64 is not first + 12
    CHECK(!OctaveDyad(three_b, 3));
    CHECK(!OctaveDyad(none, 0));
}

TEST(match_lowchord_two_or_three_notes_with_a_low_one) {
    const int32_t two[2] = {23, 40};
    const int32_t three[3] = {40, 27, 52};
    const int32_t with_gaps[4] = {-1, 20, -1, 45};

    CHECK(LowChord(two, 2));
    CHECK(LowChord(three, 3));
    CHECK(LowChord(with_gaps, 4));
}

TEST(match_lowchord_rejections) {
    const int32_t high[2] = {40, 52};  // lowest at or above E1 (28)
    const int32_t edge[2] = {28, 52};
    const int32_t one[2] = {23, -1};
    const int32_t four[4] = {23, 30, 40, 52};
    const int32_t repeated[2] = {23, 23};  // a repeat counts once
    const int32_t repeated3[3] = {23, 23, 40};

    CHECK(!LowChord(high, 2));
    CHECK(!LowChord(edge, 2));
    CHECK(!LowChord(one, 2));
    CHECK(!LowChord(four, 4));
    CHECK(!LowChord(repeated, 2));
    CHECK(LowChord(repeated3, 3));  // 23, 40 after the repeat
    CHECK(!LowChord(one, 0));
}

TEST(match_lowchord_zero_counts_as_a_note_but_not_as_lowest) {
    const int32_t open_zero[2] = {0, 40};  // two notes, but the lowest positive is 40
    const int32_t zero_low[3] = {0, 40, 23};

    CHECK(!LowChord(open_zero, 2));
    CHECK(LowChord(zero_low, 3));  // 0 counts toward 3 notes; the lowest positive is 23
}

TEST(match_compact6_packs_non_negative_and_pads_with_minus_one) {
    const int32_t in[8] = {-1, 40, -1, 0, 52, -1, -1, 64};
    int32_t out[6];
    int32_t empty[6];

    CHECK(Compact6(in, 8, out));
    CHECK_EQ(out[0], 40);
    CHECK_EQ(out[1], 0);
    CHECK_EQ(out[2], 52);
    CHECK_EQ(out[3], 64);
    CHECK_EQ(out[4], -1);
    CHECK_EQ(out[5], -1);

    CHECK(Compact6(in, 0, empty));

    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(empty[i], -1);
    }
}

TEST(match_compact6_seven_targets_fail) {
    const int32_t six[8] = {1, 2, 3, 4, 5, 6, -1, -1};
    const int32_t seven[8] = {1, 2, 3, 4, 5, 6, 7, -1};
    const int32_t eight[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int32_t out[6];

    CHECK(Compact6(six, 8, out));
    CHECK_EQ(out[5], 6);
    CHECK(!Compact6(seven, 8, out));
    CHECK(!Compact6(eight, 8, out));
}
