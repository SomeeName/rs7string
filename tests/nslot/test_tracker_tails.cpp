#include "features/nslot/nslot.h"
#include "features/nslot/tracker.h"
#include "test.h"
#include <cstring>

using namespace rs7ns;

namespace {
// An eval with tails for strings 6 and 7: midi, mask, fret, slideTo.
struct Tails {
    uint8_t ev[rs7track::kEvalSize] = {};

    void Set(int t, int32_t midi, uint32_t mask, uint8_t fret = 0, uint8_t slideTo = 0) {
        std::memcpy(ev + kEvMidi67 + 4 * t, &midi, 4);
        std::memcpy(ev + kEvMask67 + 4 * t, &mask, 4);
        ev[kEvFret67 + t] = fret;
        ev[kEvSlide67 + t] = slideTo;
    }
};
}  // namespace

TEST(tails_pitched_only_counts_sounding_strings) {
    Tails e;

    e.Set(0, -1, 0);
    e.Set(1, 0, 0);
    CHECK(!rs7track::TailPitched(e.ev));
    e.Set(1, 47, 0x400000);
    CHECK(!rs7track::TailPitched(e.ev));
    e.Set(0, 35, 0x800);
    CHECK(rs7track::TailPitched(e.ev));
}

TEST(fold_tails_ands_each_sounding_test_and_keeps_the_last) {
    Tails e;
    uint8_t last = 0x5A;

    CHECK_EQ(rs7track::FoldTails(e.ev, 0x800, false, false, 1, &last), 1);
    CHECK_EQ(last, 0x5A);  // nothing sounds: unchanged
    e.Set(0, 35, 0x800);
    e.Set(1, 30, 0);
    CHECK_EQ(rs7track::FoldTails(e.ev, 0x800, false, false, 1, &last), 0);
    CHECK_EQ(last, 0);
    CHECK_EQ(rs7track::FoldTails(e.ev, 0x800, false, true, 1, &last), 0);  // "none": string 6 slides
    CHECK_EQ(last, 1);
    e.Set(0, 35, 0x1010);
    CHECK_EQ(rs7track::FoldTails(e.ev, 0x10, true, false, 1, &last), 0);  // byte mask: string 7 has no tremolo
}

TEST(extreme_slide_continues_the_min_max_rule) {
    Tails e;
    int32_t distance = 99;

    CHECK_EQ(rs7track::ExtremeSlide(e.ev, 0, &distance), 0);
    CHECK_EQ(distance, 99);  // no fretted tail string
    e.Set(0, 35, 0, 5, 7);
    e.Set(1, 30, 0, 3, 1);
    CHECK_EQ(rs7track::ExtremeSlide(e.ev, 0, &distance), 2);  // +2 first, -2 is not larger
    CHECK_EQ(distance, -2);
    CHECK_EQ(rs7track::ExtremeSlide(e.ev, -1, &distance), -2);
    CHECK_EQ(rs7track::ExtremeSlide(e.ev, 4, &distance), 4);
}
