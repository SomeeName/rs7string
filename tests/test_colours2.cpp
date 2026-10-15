#include "core/colournative.h"
#include "core/gsnative.h"
#include "core/publish.h"
#include "test.h"
#include <cstdint>

// fix/colours2: the game builds its ColourManager ~12 s after launch; a manager that exists when
// the CM sites are written keeps the stock 6-column layout, so native_colours must be refused.
TEST(colours2_existing_manager_is_detected) {
    void* global = nullptr;

    CHECK(rs7colour::ExistingManager(0) == nullptr);
    CHECK(rs7colour::ExistingManager(reinterpret_cast<uintptr_t>(&global)) == nullptr);
    int mgr = 0;

    global = &mgr;
    CHECK(rs7colour::ExistingManager(reinterpret_cast<uintptr_t>(&global)) == &mgr);
}

// The 6-column table read with the 8-column formula is the owner's rotated tuner: state 1 slots 0..5
// land on stock state 1 slots 2..5 and state 2 slots 0..1 (blue orange green purple, dim red, dim
// yellow) and slot 6 on state 2 slot 2 (dim blue, near black).
TEST(colours2_six_column_table_read_eight_wide_is_the_reported_rotation) {
    const int lit = 1, row8 = lit * 8;
    int got[7];

    for (int s = 0; s < 7; ++s) {
        got[s] = row8 + s;  // entry index the 8-column readers use
    }

    // decode in the stock 6-column layout: entry = state*6 + slot
    CHECK_EQ(got[0] / 6, 1);
    CHECK_EQ(got[0] % 6, 2);
    CHECK_EQ(got[3] / 6, 1);
    CHECK_EQ(got[3] % 6, 5);
    CHECK_EQ(got[4] / 6, 2);
    CHECK_EQ(got[4] % 6, 0);
    CHECK_EQ(got[6] / 6, 2);
    CHECK_EQ(got[6] % 6, 2);
    // and the native offsets are the 8-column ones
    g_rs7_native_colours = 1;
    CHECK_EQ(rs7colour::PtrOffset(0, 1, 6), uint32_t(0x350 + 4 * 14));
    g_rs7_native_colours = 0;
    CHECK_EQ(rs7colour::PtrOffset(0, 1, 6), uint32_t(0x350 + 4 * 12));
}

// [neck] clite: with the native ports a guitar shows the count it was built with (info +0Bh), whatever
// song or tuner is up; capacity (loaded) is 8 strings, 4 for bass.
TEST(colours2_active_count_is_the_build_count_with_the_ports) {
    const uintptr_t saved = g_rs7_publish_ports;

    g_rs7_publish_ports = 1;

    struct Case {
        uint8_t built, active, loaded;
    };

    for (const Case c : {Case{6, 6, 8}, Case{7, 7, 8}, Case{8, 8, 8}, Case{4, 4, 4}, Case{5, 6, 8}, Case{9, 8, 8}}) {
        uint8_t info[0x48] = {};

        info[0x0B] = c.built;
        info[0x40] = 6;  // a stale active count does not matter
        rs7gs::RefreshCounts(info, false);
        CHECK_EQ(int(info[0x40]), int(c.active));
        CHECK_EQ(int(info[0x41]), int(c.loaded));
    }

    uint8_t info[0x48] = {};

    info[0x0B] = 7;
    info[0x20] = 2;
    rs7gs::RefreshCounts(info, false);  // the tail's mode[6] / mode[7] mirror string 0's
    CHECK_EQ(int(info[0x38]), 2);
    CHECK_EQ(int(info[0x3C]), 2);
    uint8_t bass[0x48] = {};

    bass[0x0B] = 4;
    bass[0x20] = 2;
    rs7gs::RefreshCounts(bass, false);
    CHECK_EQ(int(bass[0x38]), 0);
    g_rs7_publish_ports = saved;
}
