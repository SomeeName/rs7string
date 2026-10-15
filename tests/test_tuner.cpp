#include "core/tuner.h"
#include "test.h"

// Low range (W): the guitar wrong-instrument threshold is min(34, lowest open - 1). The B-phase rules (Next,
// ReverseWantsB) went with the six-slot tuner in 1.0; the native session computes its lowest open (tuner8.cpp).
TEST(tuner_lowrange_threshold_follows_the_lowest_open_note) {
    CHECK_EQ(rs7tuner::WrongThreshold(35), 34);  // B Standard 7 (B1): stock 34
    CHECK_EQ(rs7tuner::WrongThreshold(33), 32);  // Drop A / A Standard: A1
    CHECK_EQ(rs7tuner::WrongThreshold(31), 30);  // G Standard: G1
    CHECK_EQ(rs7tuner::WrongThreshold(30), 29);  // later 8-string F#1
    CHECK_EQ(rs7tuner::WrongThreshold(40), 34);  // never above stock
}
