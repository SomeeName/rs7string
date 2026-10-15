#include "core/fretgrid.h"
#include "core/publish.h"
#include "core/publish_grid.h"
#include "test.h"
#include <cstring>
#include <vector>

TEST(publish_grid_physical_order_and_barres) {
    for (unsigned n = 6; n <= 8; ++n) {
        CHECK_EQ(rs7pubgrid::PhysicalEdge(n, true), n > 6 ? n - 1 : 0u);
        CHECK_EQ(rs7pubgrid::PhysicalEdge(n, false), 5u);
        uint8_t f[8] = {1, 1, 255, 255, 255, 255, 1, 1};
        int span = 0;

        rs7pubgrid::FingerSpan(f, n, 1, &span);
        CHECK_EQ(span, int(n - 4));  // 0,1 plus all extra lanes are contiguous
    }
}

TEST(publish_grid_native_query_guards_and_actual_cells) {
    std::vector<uint8_t> grid(rs7fg::kSizeNative);

    for (unsigned n = 6; n <= 8; ++n) {
        for (unsigned s = 0; s < n; ++s) {
            for (unsigned k = 0; k < 3; ++k) {
                float xyz[3] = {float(s), float(k), 42};

                std::memcpy(rs7fg::Cell(grid.data(), 25, s) + rs7fg::kCellWorld + k * 12, xyz, 12);
                float out[3] = {};

                CHECK_EQ(rs7pubgrid::GetCellPos(grid.data(), n, 26, k, s, out), 1);
                CHECK_EQ(out[0], float(s));
                CHECK_EQ(out[1], float(k));
            }
        }
    }

    float out[3] = {11, 22, 33};

    CHECK_EQ(rs7pubgrid::GetCellPos(grid.data(), 7, 26, 0, 7, out), 0);
    CHECK_EQ(rs7pubgrid::GetCellPos(grid.data(), 8, 26, 3, 7, out), 0);
    CHECK_EQ(rs7pubgrid::GetCellPos(grid.data(), 8, 0, 0, 0, out), 0);
    CHECK_EQ(rs7pubgrid::GetCellPos(grid.data(), 8, 27, 0, 0, out), 0);
    CHECK_EQ(out[0], 11.0f);
}

TEST(publish_edge_copy_follows_publish_ports_on_the_active_count) {
    // The TU8A copy of StringAction carries PUBEDGE (7E72D0): same stock expression off, physical edge on.
    const uintptr_t saved = g_rs7_publish_ports;

    for (unsigned n = 4; n <= 8; ++n) {
        uint8_t info[0x80] = {};

        info[0x40] = uint8_t(n);
        g_rs7_publish_ports = 0;
        CHECK_EQ(Rs7Publish_EdgeCopy(info, 1), 0u);
        CHECK_EQ(Rs7Publish_EdgeCopy(info, 0), n - 1);
        g_rs7_publish_ports = 1;
        CHECK_EQ(Rs7Publish_EdgeCopy(info, 1), rs7pubgrid::PhysicalEdge(n, true));
        CHECK_EQ(Rs7Publish_EdgeCopy(info, 0), rs7pubgrid::PhysicalEdge(n, false));
    }

    g_rs7_publish_ports = saved;
}

// Rnd_FretGrid_Load (0x58F260) is polled until BuildCells returns 2: a not-ready that never clears held the song
// load forever (tests/full-test REPORT.md). The wait is bounded per grid.
TEST(publish_grid_wait_is_bounded_per_grid) {
    using rs7pubgrid::WaitEntry;
    WaitEntry t[2] = {};
    int a = 0, b = 0, c = 0;

    CHECK(!rs7pubgrid::WaitStep(t, 2, &a, 1, 1000, 5000));
    CHECK(!rs7pubgrid::WaitStep(t, 2, &a, 1, 5999, 5000));
    CHECK(!rs7pubgrid::WaitStep(t, 2, &b, 3, 5000, 5000));
    CHECK(rs7pubgrid::WaitStep(t, 2, &a, 2, 6000, 5000));  // 5 s since the first not-ready of a
    CHECK(!rs7pubgrid::WaitStep(t, 2, &b, 3, 9999, 5000));
    CHECK_EQ(t[0].calls, 3u);
    CHECK_EQ(t[0].reason, 2u);
    rs7pubgrid::WaitDone(t, 2, &a);  // a got ready: its clock restarts
    CHECK(!rs7pubgrid::WaitStep(t, 2, &a, 1, 20000, 5000));
    CHECK(!rs7pubgrid::WaitStep(t, 2, &c, 1, 20001, 5000));  // full table: c replaces the oldest entry (b)
    CHECK(rs7pubgrid::WaitStep(t, 2, &a, 1, 25000, 5000));
}

// Review F3: a wait entry belongs to one build; a failed neck is remembered per owner (the guitar).
TEST(publish_grid_wait_entry_does_not_outlive_its_build) {
    using rs7pubgrid::WaitEntry;
    WaitEntry t[2] = {};
    int grid = 0, guitar = 0, other = 0;

    CHECK(!rs7pubgrid::WaitStep(t, 2, &grid, 1, 1000, 5000, &guitar));
    CHECK(rs7pubgrid::WaitStep(t, 2, &grid, 1, 6000, 5000, &guitar));
    rs7pubgrid::MarkFailed(t, 2, &grid);
    CHECK(rs7pubgrid::Failed(t, 2, &guitar));
    CHECK(!rs7pubgrid::Failed(t, 2, &other));
    CHECK(!rs7pubgrid::Failed(t, 2, nullptr));
    // The grid's address is reused much later: a new deadline, no failure, a fresh call count.
    CHECK(!rs7pubgrid::WaitStep(t, 2, &grid, 1, 6000 + rs7pubgrid::kWaitStaleMs + 1, 5000, &guitar));
    CHECK(!rs7pubgrid::Failed(t, 2, &guitar));
    CHECK_EQ(t[0].calls, 1u);
    // ... or by another guitar right away.
    rs7pubgrid::WaitEntry u[2] = {};

    CHECK(!rs7pubgrid::WaitStep(u, 2, &grid, 1, 1000, 5000, &guitar));
    CHECK(!rs7pubgrid::WaitStep(u, 2, &grid, 1, 2000, 5000, &other));  // another owner: the 1000 ms clock is gone
    CHECK(!rs7pubgrid::WaitStep(u, 2, &grid, 1, 6500, 5000, &other));  // 4.5 s since the new owner's first poll
    CHECK(rs7pubgrid::WaitStep(u, 2, &grid, 1, 7000, 5000, &other));
    rs7pubgrid::MarkFailed(u, 2, &grid);
    CHECK(rs7pubgrid::Failed(u, 2, &other));
    rs7pubgrid::WaitDone(u, 2, nullptr, &other);  // the owner's grid got built: its entries, failed or not, go
    CHECK(!rs7pubgrid::Failed(u, 2, &other));
    CHECK(u[0].grid == nullptr);
}
