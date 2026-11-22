#include "features/spacing/spacing.h"
#include "test.h"
#include <cmath>

namespace {
bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

TEST(spacing_target_picks_the_extended_instruments_setting) {
    const float set[4] = {2.01f, 1.608f, 1.34f, 1.15f};

    CHECK(Near(rsxrspacing::Target(set, true, 5), 2.01f));
    CHECK(Near(rsxrspacing::Target(set, true, 6), 1.608f));
    CHECK(Near(rsxrspacing::Target(set, false, 7), 1.34f));
    CHECK(Near(rsxrspacing::Target(set, false, 8), 1.15f));
    // Stock instruments keep the stock spacing.
    CHECK_EQ(rsxrspacing::Target(set, true, 4), 0.0f);
    CHECK_EQ(rsxrspacing::Target(set, false, 6), 0.0f);
    CHECK_EQ(rsxrspacing::Target(set, false, 4), 0.0f);
}

// 6-string bass at stock spacing 2.68 (rows 0-3, centre z 0), scaled to 1.608: the high C (one spacing beyond the G)
// and the low B (one beyond the E) land one new spacing beyond the scaled G / E, and the width is 5 * 1.608.
TEST(spacing_scale_about_the_stock_centre_keeps_even_spacing) {
    const float d = 2.68f, want = 1.608f, k = want / d;
    const float c[3] = {0.0f, -53.653f, 0.0f};
    float z[6];

    for (int s = 0; s < 6; ++s) {
        // Stock rows 0-3 at 1.5d .. -1.5d; low B (4) at 2.5d, high C (5) at -2.5d.
        const float stock = s < 4 ? (1.5f - float(s)) * d : s == 4 ? 2.5f * d : -2.5f * d;
        float p[3] = {0.0f, -53.653f, stock};

        rsxrspacing::Scale(c, k, p);
        CHECK(Near(p[0], 0.0f) && Near(p[1], -53.653f));
        z[s] = p[2];
    }

    CHECK(Near(z[0] - z[1], want));
    CHECK(Near(z[4] - z[0], want));
    CHECK(Near(z[3] - z[5], want));
    CHECK(Near(z[4] - z[5], 5.0f * want));
}

TEST(spacing_parse_accepts_the_rows_range) {
    float v = -1.0f;

    CHECK(rsxrspacing::Parse(L"", &v) && v == 0.0f);
    CHECK(rsxrspacing::Parse(L"0", &v) && v == 0.0f);
    CHECK(rsxrspacing::Parse(L"1.608", &v) && Near(v, 1.608f));
    CHECK(rsxrspacing::Parse(L"2.01 ", &v) && Near(v, 2.01f));
    CHECK(rsxrspacing::Parse(L"0.05", &v) && v == 0.0f);  // below kMin: stock
    CHECK(!rsxrspacing::Parse(L"4.5", &v));
    CHECK(!rsxrspacing::Parse(L"wide", &v));
    CHECK(!rsxrspacing::Parse(L"1.6x", &v));
    CHECK(!rsxrspacing::Parse(L"-1", &v));
}
