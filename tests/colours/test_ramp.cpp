#include "features/colours/colours.h"
#include "ramp_fixture.h"
#include "test.h"
#include <algorithm>

namespace {
using rs7fix::Stock;
const uint32_t kRgb[2] = {0xC8C8C8, 0x00FFFF};
}  // namespace

TEST(ramp_taps_follow_d3d9_texel_centres) {
    // stock row i on 128 lines: c = 8i + 3.5 -> lines 8i+3, 8i+4 at 1/2 each; point = 8i+4
    const auto b = Rs7Ramp_Taps(15, 32, 128, true);  // row 7

    CHECK(b.size() == 2 && b[0].line == 59 && b[0].w4 == 2 && b[1].line == 60 && b[1].w4 == 2);
    const auto p = Rs7Ramp_Taps(15, 32, 128, false);

    CHECK(p.size() == 1 && p[0].line == 60);
    const auto l3 = Rs7Ramp_Taps(15, 32, 16, true);  // h = 16: c = 7.0 -> one line, weight 1

    CHECK(l3.size() == 1 && l3[0].line == 7 && l3[0].w4 == 4);
}

TEST(ramp_taps_clamp_and_never_drop_a_nonzero_line) {
    const auto lo = Rs7Ramp_Taps(1, 32, 8, true);  // c = -0.25: clamp to line 0

    CHECK(lo.size() == 1 && lo[0].line == 0 && lo[0].w4 == 4);
    const auto hi = Rs7Ramp_Taps(31, 32, 8, true);  // c = 7.25: clamp to line 7

    CHECK(hi.size() == 1 && hi[0].line == 7 && hi[0].w4 == 4);
    const auto odd = Rs7Ramp_Taps(57, 128, 8, true);  // c = 3.0625: weights 15/16, 1/16 (not quarters)

    CHECK(odd.size() == 2 && odd[0].line == 3 && odd[1].line == 4 && odd[1].w4 >= 1);
    CHECK(Rs7Ramp_Protected(1).size() == 1 && Rs7Ramp_Protected(1)[0]);
}

TEST(ramp_v6_taps_on_level0_and_1) {
    const auto a = Rs7Ramp_Taps(57, 128, 128, true);

    CHECK(a.size() == 2 && a[0].line == 56 && a[1].line == 57);
    const auto b = Rs7Ramp_Taps(57, 128, 64, true);

    CHECK(b.size() == 1 && b[0].line == 28);
    const auto c = Rs7Ramp_Taps(121, 128, 128, true);

    CHECK(c.size() == 2 && c[0].line == 120 && c[1].line == 121);
    CHECK(kRs7V6[0] == 57.0f / 128.0f && kRs7V6[1] == 121.0f / 128.0f);
}

TEST(ramp_copy_keeps_every_protected_line_at_every_level) {
    for (unsigned levels = 1; levels <= 9; ++levels) {
        const auto stock = Stock(levels);
        const auto b = Rs7Ramp_Build(stock, kRgb);
        const auto chk = Rs7Ramp_Verify(stock, b, kRgb);

        CHECK(chk.exact);

        for (unsigned k = 0; k < levels; ++k) {
            const auto prot = Rs7Ramp_Protected(stock[k].h);

            for (unsigned y = 0; y < stock[k].h; ++y) {
                if (prot[y]) {
                    for (unsigned x = 0; x < stock[k].w; ++x) {
                        CHECK_EQ(b.levels[k].px[y * stock[k].w + x], stock[k].px[y * stock[k].w + x]);
                    }
                }
            }
        }
    }
}

TEST(ramp_custom_band_is_pure_at_v6_on_levels_0_and_1) {
    const auto stock = Stock(9);
    const auto b = Rs7Ramp_Build(stock, kRgb);

    CHECK_EQ(b.customLevels, 2u);  // copy layout: 128 and 64 lines; at 32 lines V6 hits row 6's taps
    CHECK(Rs7Ramp_Verify(stock, b, kRgb).v6Pure);
    CHECK_EQ(Rs7Ramp_Verify(stock, b, kRgb).changedLines, 2u + 2u + 1u + 1u);  // L0: 56,57,120,121; L1: 28,60
}

TEST(ramp_v6_on_a_foreign_16x8_texture_reads_row_7_or_15) {
    for (int m = 0; m < 2; ++m) {
        for (bool bil : {true, false}) {
            for (const auto& t : Rs7Ramp_Taps(kRs7V6Num[m], kRs7V6Den, 128, bil)) {
                CHECK(t.line / 8 == kRs7Row6[m]);
            }
        }
    }
}

TEST(ramp_custom_line_is_the_recoloured_stock_row) {
    auto stock = Stock(1);

    for (unsigned y = 56; y < 64; ++y) {
        for (unsigned x = 0; x < 256; ++x) {
            stock[0].px[y * 256 + x] = 0x00909090;  // grey anchor
        }
    }

    const uint32_t rgb[2] = {0xC8C8C8, 0x4C4C4C};
    const auto b = Rs7Ramp_Build(stock, rgb);

    CHECK_EQ(b.levels[0].px[56 * 256 + 10], 0x00C8C8C8u);  // anchor -> colour, alpha byte copied
}

TEST(ramp_verify_detects_a_changed_protected_texel) {
    const auto stock = Stock(4);
    auto b = Rs7Ramp_Build(stock, kRgb);

    b.levels[2].px[5 * b.levels[2].w + 3] ^= 1;  // at 32 lines every line is a stock tap (c = 2i + 0.5)
    CHECK(!Rs7Ramp_Verify(stock, b, kRgb).exact);
    auto c = Rs7Ramp_Build(stock, kRgb);

    c.levels[0].px[56 * 256 + 3] ^= 1;  // a stamped line with the wrong content
    const auto chk = Rs7Ramp_Verify(stock, c, kRgb);

    CHECK(chk.exact && !chk.v6Pure);
    auto d = Rs7Ramp_Build(stock, kRgb);

    d.levels[0].px[0] ^= 1;  // an unprotected, unstamped line (line 0) changed
    CHECK(!Rs7Ramp_Verify(stock, d, kRgb).exact);
}

TEST(ramp_plan_policy) {
    const auto x8 = Rs7Ramp_Plan({22, 256, 128, 1, 1, 0}, false);

    CHECK(x8.verdict == Rs7RampVerdict::Swap && x8.exactPossible && x8.outFormat == 22);
    CHECK(Rs7Ramp_Plan({21, 256, 128, 9, 1, 0}, false).verdict == Rs7RampVerdict::Swap);   // levels: no limit
    CHECK(Rs7Ramp_Plan({22, 256, 128, 1, 0, 0}, true).verdict == Rs7RampVerdict::Refuse);  // DEFAULT pool
    CHECK(Rs7Ramp_Plan({22, 256, 256, 1, 1, 0}, true).verdict == Rs7RampVerdict::Refuse);  // size
    const uint32_t dxt1 = 0x31545844;  // MAKEFOURCC('D','X','T','1')

    CHECK(Rs7Ramp_Plan({dxt1, 256, 128, 1, 1, 0}, false).verdict == Rs7RampVerdict::Refuse);
    const auto d = Rs7Ramp_Plan({dxt1, 256, 128, 1, 1, 0}, true);

    CHECK(d.verdict == Rs7RampVerdict::Swap && !d.exactPossible && d.outFormat == 21);
    CHECK(Rs7Ramp_Plan({23 /*R5G6B5*/, 256, 128, 1, 1, 0}, true).verdict == Rs7RampVerdict::Refuse);
    CHECK(Rs7Ramp_Plan({22, 256, 128, 1, 1, 0x400 /*AUTOGENMIPMAP*/}, true).verdict == Rs7RampVerdict::Refuse);
    CHECK(Rs7Ramp_Plan({22, 256, 128, 0, 1, 0}, true).verdict == Rs7RampVerdict::Refuse);  // no levels
}

TEST(ramp_hash_and_row_stats) {
    const auto a = Stock(2), b = Stock(2);

    CHECK_EQ(Rs7Ramp_Hash(a), Rs7Ramp_Hash(b));
    auto c = Stock(2);

    c[1].px[0] ^= 1;
    CHECK(Rs7Ramp_Hash(a) != Rs7Ramp_Hash(c));
    CHECK_EQ(Rs7Ramp_RowStats(a[0]).nonUniformRows, 16u);
    Rs7Image u;  // 16 uniform rows, rows 7 and 15 grey

    u.w = 4;
    u.h = 128;
    u.px.resize(4 * 128);

    for (unsigned y = 0; y < 128; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const unsigned row = y / 8;

            u.px[y * 4 + x] = (row == 7 || row == 15) ? 0x00808080u + x : 0x00102030u + x;
        }
    }

    const auto s = Rs7Ramp_RowStats(u);

    CHECK(s.nonUniformRows == 0 && !s.grey7 && !s.grey15);  // +x breaks R=G=B

    for (unsigned y = 56; y < 64; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            u.px[y * 4 + x] = 0x00777777u;
        }
    }

    CHECK(Rs7Ramp_RowStats(u).grey7);
}
