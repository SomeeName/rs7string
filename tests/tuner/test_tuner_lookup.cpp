// RsxrTuner_SevenFor / _EighthFor / _BassFor (the manifest keys per offset array) and rsxrtuner::LowestOpen.
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "test.h"
#include <cstring>

namespace {
int g_offs[3][6];  // three distinct offset arrays (only the addresses are keys)

struct Clear {
    Clear() { rsxrtuner::g_state.keys.Clear(); }

    ~Clear() { rsxrtuner::g_state.keys.Clear(); }
};

void Put(const int* offs, bool found, int t6, bool found7, int t7, int bass) {
    rsxrtuner::Key k{};

    k.found = found;
    k.t6 = t6;
    k.found7 = found7;
    k.t7 = t7;
    k.bass = bass;
    rsxrtuner::g_state.keys.Put(offs, k);
}

struct Session {
    alignas(4) uint8_t b[0x400];

    Session() { std::memset(b, 0, sizeof b); }

    void Set(int count, const int* open, int n) {
        b[rsxrtu::kCount] = uint8_t(count);

        for (int i = 0; i < n; ++i) {
            std::memcpy(b + rsxrtu::kOpen + 4 * i, &open[i], 4);
        }
    }
};
}  // namespace

TEST(tuner_lookup_seven_needs_found) {
    Clear guard;
    int t6 = 99;

    CHECK(!RsxrTuner_SevenFor(g_offs[0], &t6));  // no key
    CHECK_EQ(t6, 99);

    Put(g_offs[0], true, -5, false, 0, 0);
    Put(g_offs[1], false, -7, false, 0, 5);
    CHECK(RsxrTuner_SevenFor(g_offs[0], &t6));
    CHECK_EQ(t6, -5);

    t6 = 99;
    CHECK(!RsxrTuner_SevenFor(g_offs[1], &t6));  // a bass key is not a 7-string
    CHECK_EQ(t6, 99);
    CHECK(!RsxrTuner_SevenFor(nullptr, &t6));
}

TEST(tuner_lookup_eighth_needs_found_and_found7) {
    Clear guard;
    int t7 = 99;

    Put(g_offs[0], true, -5, false, -9, 0);  // 7 strings only
    Put(g_offs[1], true, -5, true, -9, 0);   // 8 strings
    Put(g_offs[2], false, 0, true, -3, 0);   // found7 without found
    CHECK(!RsxrTuner_EighthFor(g_offs[0], &t7));
    CHECK_EQ(t7, 99);
    CHECK(RsxrTuner_EighthFor(g_offs[1], &t7));
    CHECK_EQ(t7, -9);

    t7 = 99;
    CHECK(!RsxrTuner_EighthFor(g_offs[2], &t7));
    CHECK_EQ(t7, 99);
    CHECK(!RsxrTuner_EighthFor(nullptr, &t7));
}

TEST(tuner_lookup_bass_only_when_not_found) {
    Clear guard;

    Put(g_offs[0], false, 0, false, 0, 5);
    Put(g_offs[1], false, 0, false, 0, 6);
    Put(g_offs[2], true, -5, false, 0, 6);  // found wins: not a bass
    CHECK_EQ(RsxrTuner_BassFor(g_offs[0]), 5);
    CHECK_EQ(RsxrTuner_BassFor(g_offs[1]), 6);
    CHECK_EQ(RsxrTuner_BassFor(g_offs[2]), 0);
    CHECK_EQ(RsxrTuner_BassFor(nullptr), 0);

    rsxrtuner::g_state.keys.Clear();
    CHECK_EQ(RsxrTuner_BassFor(g_offs[0]), 0);  // unknown
}

TEST(tuner_lowestopen_zero_count_gives_127) {
    Session s;
    const int open[2] = {10, 20};

    s.Set(0, open, 2);
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 127);
}

TEST(tuner_lowestopen_takes_the_minimum_of_count_strings) {
    Session s;
    const int open[8] = {40, 45, 50, 55, 59, 64, 35, 30};

    s.Set(6, open, 8);
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 40);  // strings 6, 7 are beyond the count
    s.Set(7, open, 8);
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 35);
    s.Set(8, open, 8);
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 30);
}

TEST(tuner_lowestopen_clamps_count_above_eight) {
    Session s;
    const int open[9] = {40, 45, 50, 55, 59, 64, 35, 30, 10};

    s.Set(12, open, 9);  // the ninth slot is not a string
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 30);
}

TEST(tuner_lowestopen_four_string_count) {
    Session s;
    const int open[6] = {28, 33, 38, 43, 12, 12};

    s.Set(4, open, 6);
    CHECK_EQ(rsxrtuner::LowestOpen(s.b), 28);
}
