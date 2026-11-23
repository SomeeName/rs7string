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

TEST(publish_grid_finger_anchor_is_the_lowest_physical_string) {
    // Seven strings: slot 6 is the low B, below slot 0 (E). A B+E barre found first on the E anchors on the B.
    const uint8_t barre7[8] = {1, 1, 255, 255, 255, 255, 1, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre7, 7, 0), 6u);
    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre7, 7, 6), 6u);

    // Eight strings: slot 7 (low F#) is below slot 6 (low B).
    const uint8_t barre8[8] = {2, 255, 255, 255, 255, 255, 2, 2};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre8, 8, 0), 7u);

    // A finger only on stock strings keeps its first string; another finger is ignored.
    const uint8_t stock[8] = {255, 1, 1, 3, 255, 255, 3, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(stock, 7, 1), 1u);
    CHECK_EQ(rsxrpubgrid::FingerAnchor(stock, 7, 3), 6u);

    // Six strings, an unused finger or an out-of-range string: unchanged.
    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre7, 6, 0), 0u);
    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre7, 7, 2), 2u);
    CHECK_EQ(rsxrpubgrid::FingerAnchor(barre7, 7, 9), 9u);
}

TEST(publish_grid_bass_finger_anchor_and_span_use_the_bass_order) {
    // Bass slots: E A D G in 0-3, low B in 4, high C in 5. Physical order B E A D G C.
    int span = 0;

    // Low B + E barre, found first on the E: anchored on the B, two strings wide (slot order said five).
    const uint8_t be[8] = {1, 255, 255, 255, 1, 255, 255, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(be, 6, 0, true), 4u);
    rsxrpubgrid::FingerSpan(be, 6, 1, &span, true);
    CHECK_EQ(span, 2);
    rsxrpubgrid::FingerSpan(be, 6, 1, &span);
    CHECK_EQ(span, 5);  // the guitar order, for comparison

    // G + high C barre on six strings: two strings wide, anchored on the G.
    const uint8_t gc[8] = {255, 255, 255, 2, 255, 2, 255, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(gc, 6, 3, true), 3u);
    rsxrpubgrid::FingerSpan(gc, 6, 2, &span, true);
    CHECK_EQ(span, 2);

    // B E A barre: three strings from the B. A barre on E A only keeps the E.
    const uint8_t bea[8] = {1, 1, 255, 255, 1, 255, 255, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(bea, 6, 0, true), 4u);
    rsxrpubgrid::FingerSpan(bea, 6, 1, &span, true);
    CHECK_EQ(span, 3);
    const uint8_t ea[8] = {1, 1, 255, 255, 255, 255, 255, 255};

    CHECK_EQ(rsxrpubgrid::FingerAnchor(ea, 6, 0, true), 0u);
    rsxrpubgrid::FingerSpan(ea, 6, 1, &span, true);
    CHECK_EQ(span, 2);

    CHECK_EQ(rsxrpubgrid::FingerRank(4, 6, true), 0u);
    CHECK_EQ(rsxrpubgrid::FingerRank(0, 6, true), 1u);
    CHECK_EQ(rsxrpubgrid::FingerRank(5, 6, true), 5u);
}

TEST(publish_grid_finger_anchor_and_span_match_open_pitch_order_for_every_pattern) {
    // Every finger pattern (none, thumb 0, fingers 1-4 on each string) of every extended instrument. The oracle
    // orders the template slots by their open pitch, independent of FingerRank: the anchor must be the lowest
    // string with that finger and the span must run from it to the highest.
    struct Case {
        unsigned n;
        bool bass;
        int midi[8];
    };

    const Case cases[] = {
        {7, false, {40, 45, 50, 55, 59, 64, 35, 0}},
        {8, false, {40, 45, 50, 55, 59, 64, 35, 30}},
        {6, true, {28, 33, 38, 43, 23, 0, 0, 0}},  // five-string bass: slot 5 unused
        {6, true, {28, 33, 38, 43, 23, 48, 0, 0}},
    };
    const uint8_t values[] = {255, 0, 1, 2, 3, 4};
    unsigned checked = 0;

    for (const Case& c : cases) {
        const unsigned used = c.bass && c.midi[5] == 0 ? 5 : c.n;  // strings that can hold a finger
        unsigned total = 1;

        for (unsigned i = 0; i < used; ++i) {
            total *= 6;
        }

        for (unsigned code = 0; code < total; ++code) {
            uint8_t f[8] = {255, 255, 255, 255, 255, 255, 255, 255};
            unsigned k = code;

            for (unsigned i = 0; i < used; ++i, k /= 6) {
                f[i] = values[k % 6];
            }

            for (unsigned s = 0; s < used; ++s) {
                if (f[s] == 255) {
                    continue;
                }

                int lo = 1000, hi = -1, loSlot = -1, ranksBelowLo = 0, ranksBelowHi = 0;

                for (unsigned i = 0; i < used; ++i) {
                    if (f[i] == f[s] && c.midi[i] < lo) {
                        lo = c.midi[i];
                        loSlot = int(i);
                    }

                    if (f[i] == f[s] && c.midi[i] > hi) {
                        hi = c.midi[i];
                    }
                }

                for (unsigned i = 0; i < used; ++i) {
                    ranksBelowLo += c.midi[i] < lo;
                    ranksBelowHi += c.midi[i] < hi;
                }

                int span = 0;

                rsxrpubgrid::FingerSpan(f, c.n, f[s], &span, c.bass);
                CHECK_EQ(rsxrpubgrid::FingerAnchor(f, c.n, s, c.bass), unsigned(loSlot));
                CHECK_EQ(span, ranksBelowHi - ranksBelowLo + 1);
                ++checked;
            }
        }
    }

    CHECK(checked > 1000000u);
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

TEST(publish_grid_barre_fit_reaches_the_span_at_the_neck_gap) {
    const auto approx = [](float a, float b) {
        return a > b - 1e-4f && a < b + 1e-4f;
    };
    const float g = 1.608f;  // the guitar gap, the clip's own

    // Up to six strings at the clip's gap: stock (the seek (span - 1) / 5, no scale).
    for (int span = -1; span <= 6; ++span) {
        CHECK(approx(rsxrpubgrid::BarreClipScale(span, g, g), 1.0f));
        CHECK(approx(rsxrpubgrid::BarreEntityScale(span, g, g), 1.0f));
    }

    // Seven and eight strings: the clip stays at its end, the capsule grows to six / seven gaps.
    CHECK(approx(rsxrpubgrid::BarreClipScale(7, g, g), 1.0f));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(7, g, g), 6.0f / 5.0f));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(8, g, g), 7.0f / 5.0f));

    // A wider gap within the clip: a longer seek, no scale (bass: 2 strings = one gap of 2.68).
    CHECK(approx(rsxrpubgrid::BarreClipScale(2, 2.68f, g), 2.68f / g));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(2, 2.68f, g), 1.0f));
    // ... and beyond it: the clip's end, then the scale (a 6-string bass full barre: 5 gaps of 2.68).
    CHECK(approx(rsxrpubgrid::BarreClipScale(6, 2.68f, g), 1.0f));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(6, 2.68f, g), 5.0f * 2.68f / (5.0f * g)));

    // A narrow 8-string neck as wide as a six-string one: the full barre is the clip's end exactly, unscaled.
    const float narrow = 5.0f * g / 7.0f;

    CHECK(approx(rsxrpubgrid::BarreClipScale(8, narrow, g), 1.0f));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(8, narrow, g), 1.0f));
    // ... and its two-string barre a shorter seek: one narrow gap of the 1/5 the stock seek gives.
    CHECK(approx(rsxrpubgrid::BarreClipScale(2, narrow, g), narrow / g));

    // Bad gaps keep the stock seek.
    CHECK(approx(rsxrpubgrid::BarreClipScale(7, 0.0f, g), 1.0f));
    CHECK(approx(rsxrpubgrid::BarreEntityScale(7, g, 0.0f), 1.0f));
}
