#include "features/fretgrid/fretgrid.h"
#include "features/publish/hooks.h"
#include "features/string7/string7.h"
#include "test.h"
#include "testutil.h"
#include <cstring>
#include <vector>

TEST(publish_grid_physical_order_and_barres) {
    for (unsigned n = 6; n <= 8; ++n) {
        CHECK_EQ(rsxrpubgrid::PhysicalEdge(n, true), n > 6 ? n - 1 : 0u);
        CHECK_EQ(rsxrpubgrid::PhysicalEdge(n, false), 5u);
        uint8_t f[8] = {1, 1, 255, 255, 255, 255, 1, 1};
        int span = 0;

        rsxrpubgrid::FingerSpan(f, n, 1, &span);
        CHECK_EQ(span, int(n - 4));  // 0,1 plus all extra lanes are contiguous
    }
}

TEST(publish_grid_native_query_guards_and_actual_cells) {
    std::vector<uint8_t> grid(rsxrfg::kSizeNative);

    for (unsigned n = 6; n <= 8; ++n) {
        for (unsigned s = 0; s < n; ++s) {
            for (unsigned k = 0; k < 3; ++k) {
                float xyz[3] = {float(s), float(k), 42};

                std::memcpy(rsxrfg::Cell(grid.data(), 25, s) + rsxrfg::kCellWorld + k * 12, xyz, 12);
                float out[3] = {};

                CHECK_EQ(rsxrpubgrid::GetCellPos(grid.data(), n, 26, k, s, out), 1);
                CHECK_EQ(out[0], float(s));
                CHECK_EQ(out[1], float(k));
            }
        }
    }

    float out[3] = {11, 22, 33};

    CHECK_EQ(rsxrpubgrid::GetCellPos(grid.data(), 7, 26, 0, 7, out), 0);
    CHECK_EQ(rsxrpubgrid::GetCellPos(grid.data(), 8, 26, 3, 7, out), 0);
    CHECK_EQ(rsxrpubgrid::GetCellPos(grid.data(), 8, 0, 0, 0, out), 0);
    CHECK_EQ(rsxrpubgrid::GetCellPos(grid.data(), 8, 27, 0, 0, out), 0);
    CHECK_EQ(out[0], 11.0f);
}

TEST(publish_edge_copy_follows_publish_ports_on_the_active_count) {
    // The TU8A copy of StringAction carries PUBEDGE (7E72D0): same stock expression off, physical edge on.
    rsxrtest::Restore restore_g_rsxr_publish_ports(g_rsxr_publish_ports);

    for (unsigned n = 4; n <= 8; ++n) {
        uint8_t info[0x80] = {};

        info[0x40] = uint8_t(n);
        g_rsxr_publish_ports = 0;
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 1), 0u);
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 0), n - 1);
        g_rsxr_publish_ports = 1;
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 1), rsxrpubgrid::PhysicalEdge(n, true));
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 0), rsxrpubgrid::PhysicalEdge(n, false));
    }
}

TEST(publish_bass_edges_follow_the_low_b_and_high_c_in_both_wrappers) {
    rsxrtest::Restore restore_g_rsxr_publish_ports(g_rsxr_publish_ports);
    uint8_t info[0x80] = {};

    info[0] = 2;
    g_rsxr_publish_ports = 1;

    for (unsigned n = 4; n <= 6; ++n) {
        info[0xB] = info[0x40] = uint8_t(n);
        const unsigned low = n > 4 ? 4u : 0u, high = n == 6 ? 5u : 3u;

        CHECK_EQ(RsxrPublish_Edge(info, 1), low);
        CHECK_EQ(RsxrPublish_Edge(info, 0), high);
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 1), low);
        CHECK_EQ(RsxrPublish_EdgeCopy(info, 0), high);
    }

    g_rsxr_publish_ports = 0;
    CHECK_EQ(RsxrPublish_EdgeCopy(info, 1), 0u);
    CHECK_EQ(RsxrPublish_EdgeCopy(info, 0), 5u);
}

TEST(publish_guitar_cleanup_keeps_the_other_players_failure_and_deadline) {
    uint8_t guitar[0x50] = {}, other[0x50] = {}, waiting[0x50] = {};
    uint8_t grids[3] = {};

    RsxrPublish_GridForgetAll();
    CHECK(!rsxrpubgrid::WaitStep(g_rsxr_publish_wait, 8, &grids[0], 1, 1000, 5000, guitar));
    CHECK(!rsxrpubgrid::WaitStep(g_rsxr_publish_wait, 8, &grids[1], 1, 1000, 5000, other));
    CHECK(!rsxrpubgrid::WaitStep(g_rsxr_publish_wait, 8, &grids[2], 1, 1000, 5000, waiting));
    rsxrpubgrid::MarkFailed(g_rsxr_publish_wait, 8, &grids[0]);
    rsxrpubgrid::MarkFailed(g_rsxr_publish_wait, 8, &grids[1]);
    Rsxr_GuitarGone(guitar);
    CHECK(!RsxrPublish_NeckFailed(guitar));
    CHECK(RsxrPublish_NeckFailed(other));
    CHECK(rsxrpubgrid::WaitStep(g_rsxr_publish_wait, 8, &grids[2], 1, 6000, 5000, waiting));
    RsxrPublish_GridForgetAll();
}

// Rnd_FretGrid_Load (0x58F260) is polled until BuildCells returns 2: a not-ready that never clears held the song
// load forever (tests/full-test REPORT.md). The wait is bounded per grid.
TEST(publish_grid_wait_is_bounded_per_grid) {
    using rsxrpubgrid::WaitEntry;
    WaitEntry t[2] = {};
    int a = 0, b = 0, c = 0;

    CHECK(!rsxrpubgrid::WaitStep(t, 2, &a, 1, 1000, 5000));
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &a, 1, 5999, 5000));
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &b, 3, 5000, 5000));
    CHECK(rsxrpubgrid::WaitStep(t, 2, &a, 2, 6000, 5000));  // 5 s since the first not-ready of a
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &b, 3, 9999, 5000));
    CHECK_EQ(t[0].calls, 3u);
    CHECK_EQ(t[0].reason, 2u);
    rsxrpubgrid::WaitDone(t, 2, &a);  // a got ready: its clock restarts
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &a, 1, 20000, 5000));
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &c, 1, 20001, 5000));  // full table: c replaces the oldest entry (b)
    CHECK(rsxrpubgrid::WaitStep(t, 2, &a, 1, 25000, 5000));
}

// Review F3: a wait entry belongs to one build; a failed neck is remembered per owner (the guitar).
TEST(publish_grid_wait_entry_does_not_outlive_its_build) {
    using rsxrpubgrid::WaitEntry;
    WaitEntry t[2] = {};
    int grid = 0, guitar = 0, other = 0;

    CHECK(!rsxrpubgrid::WaitStep(t, 2, &grid, 1, 1000, 5000, &guitar));
    CHECK(rsxrpubgrid::WaitStep(t, 2, &grid, 1, 6000, 5000, &guitar));
    rsxrpubgrid::MarkFailed(t, 2, &grid);
    CHECK(rsxrpubgrid::Failed(t, 2, &guitar));
    CHECK(!rsxrpubgrid::Failed(t, 2, &other));
    CHECK(!rsxrpubgrid::Failed(t, 2, nullptr));
    // The grid's address is reused much later: a new deadline, no failure, a fresh call count.
    CHECK(!rsxrpubgrid::WaitStep(t, 2, &grid, 1, 6000 + rsxrpubgrid::kWaitStaleMs + 1, 5000, &guitar));
    CHECK(!rsxrpubgrid::Failed(t, 2, &guitar));
    CHECK_EQ(t[0].calls, 1u);
    // ... or by another guitar right away.
    rsxrpubgrid::WaitEntry u[2] = {};

    CHECK(!rsxrpubgrid::WaitStep(u, 2, &grid, 1, 1000, 5000, &guitar));
    CHECK(!rsxrpubgrid::WaitStep(u, 2, &grid, 1, 2000, 5000, &other));  // another owner: the 1000 ms clock is gone
    CHECK(!rsxrpubgrid::WaitStep(u, 2, &grid, 1, 6500, 5000, &other));  // 4.5 s since the new owner's first poll
    CHECK(rsxrpubgrid::WaitStep(u, 2, &grid, 1, 7000, 5000, &other));
    rsxrpubgrid::MarkFailed(u, 2, &grid);
    CHECK(rsxrpubgrid::Failed(u, 2, &other));
    rsxrpubgrid::WaitDone(u, 2, nullptr, &other);  // the owner's grid got built: its entries, failed or not, go
    CHECK(!rsxrpubgrid::Failed(u, 2, &other));
    CHECK(u[0].grid == nullptr);
}
