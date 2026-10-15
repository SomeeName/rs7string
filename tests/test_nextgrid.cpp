#include "core/neck.h"
#include "core/nextgrid.h"
#include "test.h"
#include <cstring>
#include <vector>

using namespace rs7ng;

namespace {
struct SpawnCall {
    const uint8_t* note;
    void* self;
    uint32_t dt, t, s, f;
};

std::vector<SpawnCall> g_calls;

void __stdcall FakeSpawn(void* note, void* self, uint32_t dt, uint32_t t, uint32_t s, uint32_t f) {
    g_calls.push_back({static_cast<const uint8_t*>(note), self, dt, t, s, f});
}
}  // namespace

TEST(nextgrid_layout_arithmetic) {
    CHECK_EQ(kTimeBytesStock, 0x270u);
    CHECK_EQ(kTimeBytesNative, 0x340u);
    CHECK_EQ(kTimeStock + kTimeBytesStock, 0xD00u);  // stock time rows end at the counts
    CHECK_EQ(kGridStock, 0xD1Cu);                    // tree header +D04h..D1Bh, then the cookie
    CHECK_EQ(kGridNative, 0x105Cu);
    CHECK_EQ(kFrameNative, 0x1074u);
    CHECK(kFrameNative > 0x1000u);  // NGP's probe is needed
    CHECK(kFrameStock < 0x1000u);
    CHECK_EQ(kSetEaxBias, 0x347u);  // NG1: stock lea edi,[esi+eax*4]
    CHECK_EQ(TimeOff(0, 0), 0xD1Cu);
    CHECK_EQ(TimeOff(5, 25), kTimeNative + 5 * 0x68 + 25 * 4);
    CHECK_EQ(TimeOff(7, 25) + 4, kGridNative);  // the last cell ends the grid
    CHECK_EQ(rs7neck::kNgRow6Time, TimeOff(6, 0));
    CHECK_EQ(rs7neck::kRow6Ptr, 6u * 26u * 4u);  // pointer row 6 is the same in both layouts
    CHECK(kCapacity * kRowBytes <= kTimeStock);  // pointer rows 0-7 lie inside the 26-row pointer block
    // the frame shift is exactly the growth of the grid
    CHECK_EQ(kFrameNative - kFrameStock, kGridNative - kGridStock);
}

TEST(nextgrid_set_accepts_eight_strings_and_26_frets) {
    for (uint32_t s = 0; s < 8; ++s) {
        CHECK(SetAccepts(s, 25));
    }

    CHECK(!SetAccepts(8, 0));
    CHECK(!SetAccepts(255, 0));
    CHECK(!SetAccepts(0, 26));
    CHECK(!SetAccepts(7, 255));
}

TEST(nextgrid_configure_sets_the_switch) {
    const uintptr_t saved = g_rs7_native_nextgrid;

    Configure(true);
    CHECK_EQ(g_rs7_native_nextgrid, uintptr_t(1));
    Configure(false);
    CHECK_EQ(g_rs7_native_nextgrid, uintptr_t(0));
    g_rs7_native_nextgrid = saved;
}

TEST(nextgrid_row6_native_spawns_from_the_native_time_row_only_at_six_strings) {
    std::vector<uint8_t> g(kGridNative, 0);
    uint8_t notes[3][0x80] = {};

    notes[0][0x51] = 1;  // single
    notes[1][0x52] = 1;  // chord
                         // notes[2]: neither: no spawn
    const uint32_t frets = 5;
    const uint8_t* ptrs[5] = {nullptr, notes[0], notes[1], notes[2], nullptr};

    std::memcpy(g.data() + rs7neck::kRow6Ptr, ptrs, sizeof ptrs);
    g_rs7_fn_spawn = reinterpret_cast<uintptr_t>(&FakeSpawn);
    int self = 0;

    g_calls.clear();
    CHECK_EQ(Rs7_NeckRow6N(g.data(), &self, 11, 22, frets, 6), 1);  // first spawned fret >= 1
    CHECK_EQ(g_calls.size(), size_t(2));

    if (g_calls.size() == 2) {
        CHECK(g_calls[0].note == notes[0] && g_calls[0].s == 6 && g_calls[0].f == 1);
        CHECK(g_calls[1].note == notes[1] && g_calls[1].f == 2);
    }
}

TEST(nextgrid_row6_native_is_left_to_the_stock_loops_from_seven_strings) {
    std::vector<uint8_t> g(kGridNative, 0);
    uint8_t note[0x80] = {};

    note[0x51] = 1;
    const uint8_t* p = note;

    std::memcpy(g.data() + rs7neck::kRow6Ptr + 4, &p, sizeof p);
    g_rs7_fn_spawn = reinterpret_cast<uintptr_t>(&FakeSpawn);
    g_calls.clear();
    CHECK_EQ(Rs7_NeckRow6N(g.data(), nullptr, 1, 2, 5, 7), -1);
    CHECK_EQ(Rs7_NeckRow6N(g.data(), nullptr, 1, 2, 5, 8), -1);
    CHECK(g_calls.empty());
    CHECK_EQ(Rs7_NeckRow6N(g.data(), nullptr, 1, 2, 5, 4), 1);  // <= 6 strings: the NG9 path
    g_calls.clear();
}

TEST(nextgrid_row6_empty_row_is_a_pure_read) {
    std::vector<uint8_t> g(kGridNative, 0xCD);

    std::memset(g.data() + rs7neck::kRow6Ptr, 0, 26 * 4);
    const std::vector<uint8_t> before = g;

    g_calls.clear();
    CHECK_EQ(Rs7_NeckRow6N(g.data(), nullptr, 1, 2, 24, 6), -1);
    CHECK(g == before);
    CHECK(g_calls.empty());
}
