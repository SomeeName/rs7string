#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>

// Low range (W): the guitar wrong-instrument threshold is min(34, lowest open - 1). The B-phase rules (Next,
// ReverseWantsB) went with the six-slot tuner in 1.0; the native session computes its lowest open (hooks_session.cpp).
TEST(tuner_lowrange_threshold_follows_the_lowest_open_note) {
    CHECK_EQ(rs7tuner::WrongThreshold(35), 34);  // B Standard 7 (B1): stock 34
    CHECK_EQ(rs7tuner::WrongThreshold(33), 32);  // Drop A / A Standard: A1
    CHECK_EQ(rs7tuner::WrongThreshold(31), 30);  // G Standard: G1
    CHECK_EQ(rs7tuner::WrongThreshold(30), 29);  // later 8-string F#1
    CHECK_EQ(rs7tuner::WrongThreshold(40), 34);  // never above stock
}

TEST(tuner_bass_low_b_grey_unless_a_guitar_target_came_with_it) {
    CHECK(rs7tuner::BassLowB(true, false, 0));        // single player extended bass
    CHECK(rs7tuner::BassLowB(true, true, 60000));     // a bass song after a guitar song
    CHECK(!rs7tuner::BassLowB(true, true, 3));        // co-op: the guitar's target read in the same burst
    CHECK(!rs7tuner::BassLowB(false, false, 60000));  // a guitar (or a 4-string bass) keeps slot 4 green
}

TEST(tuner_autotune_restarts_the_delay_when_the_selected_string_changes) {
    rs7test::Restore restore_auto(g_rs7_tuner_auto);
    rs7test::Restore restore_slot(rs7tuner::g_state.autos[0]);
    uint8_t session[rs7tu::kStockSize] = {};
    auto& a = rs7tuner::g_state.autos[0];
    const uint32_t oldTick = GetTickCount() - 30000;

    g_rs7_tuner_auto = 30;
    a = rs7tuner::Auto{session, oldTick, 6};
    rs7tuner::UncheckedField<int>(session, rs7tu::kCur) = 0;
    CHECK_EQ(Rs7_TunerAutotune(session), 0u);
    CHECK_EQ(a.cur, 0);
    CHECK(a.tick != oldTick);
    CHECK_EQ(Rs7_TunerAutotune(session), 0u);
    a.tick = GetTickCount() - 30000;
    CHECK_EQ(Rs7_TunerAutotune(session), 1u);
    CHECK_EQ(Rs7_TunerAutotune(session), 0u);
}
