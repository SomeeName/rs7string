#include "core/frets.h"
#include "test.h"
#include <cmath>

namespace {
bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

// Menu snapshot (docs/ARCHITECTURE.md): string s at local z 4.0196 - 1.6078*s.
TEST(frets_extend_moves_half_a_spacing_towards_string_6_and_keeps_the_string_5_end) {
    const float a0[3] = {0.0f, -53.653f, 4.0196f}, a1[3] = {0.0f, -53.653f, 4.0196f - 1.6078f};
    const float mid[3] = {0.0f, -53.653f, 0.271f};
    rs7frets::Extend x{};

    CHECK(rs7frets::Compute(a0, a1, mid, 4.61f, &x));
    CHECK(Near(x.spacing, 1.6078f));
    CHECK(Near(x.shift[0], 0.0f) && Near(x.shift[1], 0.0f) && Near(x.shift[2], 0.8039f));
    CHECK(Near(x.scale, (4.61f + 0.8039f) / 4.61f));
    // Bar centred on z c with half length L: the far end (string 5 side) stays, the near end grows by d.
    const float c = 0.271f, L = 4.61f;
    const float lo = c + x.shift[2] - x.scale * L, hi = c + x.shift[2] + x.scale * L;

    CHECK(Near(lo, c - L, 1e-3f));
    CHECK(Near(hi, c + L + 1.6078f, 1e-3f));
}

// Inverted strings (attach points mirrored, CONFIRMED in game 2026-10-15: shift -0.80 for normal order): string 0
// at -4.02, the stock bar still centred at +0.271, so its string-0 end has the small margin (0.32) and the string-5
// end the wide one (0.86). The string-6 end gets the wide margin, the string-5 end stays.
TEST(frets_extend_inverted_keeps_the_wider_margin_beyond_string_6) {
    const float a0[3] = {0.0f, 0.0f, -4.0196f}, a1[3] = {0.0f, 0.0f, -4.0196f + 1.6078f};
    const float mid[3] = {0.0f, 0.0f, 0.271f};
    const float L = 4.61f;
    rs7frets::Extend x{};

    CHECK(rs7frets::Compute(a0, a1, mid, L, &x));
    const float lo = mid[2] + x.shift[2] - x.scale * L, hi = mid[2] + x.shift[2] + x.scale * L;
    const float s6 = -4.0196f - 1.6078f, s5 = -4.0196f + 5.0f * 1.6078f;

    CHECK(Near(hi, mid[2] + L, 1e-3f));            // string-5 end unchanged
    CHECK(Near(s6 - lo, mid[2] + L - s5, 1e-3f));  // margin beyond string 6 = the stock 0.86
    CHECK(Near(mid[2] + L - s5, 0.8614f, 2e-3f));
}

TEST(frets_extend_rejects_degenerate_input) {
    const float a[3] = {1.0f, 2.0f, 3.0f};
    rs7frets::Extend x{};

    x.scale = 7.0f;
    CHECK(!rs7frets::Compute(a, a, a, 4.61f, &x));
    const float b[3] = {1.0f, 2.0f, 4.0f};

    CHECK(!rs7frets::Compute(a, b, a, 0.0f, &x));
    CHECK(x.scale == 7.0f);  // untouched
}

TEST(frets_half_length_parse) {
    float v = 0.0f;

    CHECK(rs7frets::ParseHalfLen(L" 4.61 ", &v) && v == 4.61f);
    CHECK(rs7frets::ParseHalfLen(L"1", &v) && v == 1.0f);
    CHECK(rs7frets::ParseHalfLen(L"20", &v) && v == 20.0f);

    for (const wchar_t* bad : {L"", L"x", L"0.99", L"20.5", L"4.6x", L"nan", L"inf"}) {
        CHECK(!rs7frets::ParseHalfLen(bad, &v));
    }
}

// The highway floor: the floor (fret-wire bottom end b) on string 6's side of string 0 (inverted strings) moves one
// spacing out.
TEST(frets_floor_shift_only_on_string_6_side) {
    const float w0[3] = {0.0f, 0.0f, 4.0196f}, w1[3] = {0.0f, 0.0f, 4.0196f - 1.6078f};
    float out[3] = {9.0f, 9.0f, 9.0f};
    const float bInv[3] = {1.0f, -53.6f, 5.99f};  // beyond string 0, string 6's side: inverted

    CHECK(rs7frets::FloorShift(w0, w1, bInv, out));
    CHECK(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 1.6078f));
    // New floor 1.97 beyond string 6 (5.6274), like the stock floor beyond the outer string.
    CHECK(Near(bInv[2] + out[2] - (2.0f * w0[2] - w1[2]), 5.99f - w0[2], 1e-3f));
    const float bNorm[3] = {1.0f, -53.6f, -5.99f};  // beyond string 5: normal order, stock

    out[2] = 9.0f;
    CHECK(!rs7frets::FloorShift(w0, w1, bNorm, out));
    CHECK(out[2] == 9.0f);
    // Mirrored (string 6 towards -z) works the same way.
    const float m0[3] = {0.0f, 0.0f, -4.0f}, m1[3] = {0.0f, 0.0f, -2.4f}, bm[3] = {0.0f, 0.0f, -6.0f};

    CHECK(rs7frets::FloorShift(m0, m1, bm, out) && Near(out[2], -1.6f));
    CHECK(!rs7frets::FloorShift(w0, w0, bInv, out));  // degenerate spacing
}

// ---- chord frame span (B2): derived from the lane positions, no scale factor ---------------------------------------
// Menu snapshot geometry: d = 1.6078, lanes 0-5 at +-4.0196 (symmetric), stock floor b 1.97 beyond the outer lane.
namespace {
constexpr float kD = 1.6078f, kM = 1.97f;
const float kStringsNormal0[3] = {0.0f, 0.0f, 4.0196f}, kStringsNormal1[3] = {0.0f, 0.0f, 4.0196f - kD};
const float kStringsRev0[3] = {0.0f, 0.0f, -4.0196f}, kStringsRev1[3] = {0.0f, 0.0f, -4.0196f + kD};
const float kFloor[3] = {0.0f, 0.0f, -5.99f};  // the stock floor end is at -5.99 in both string orders

// The world interval [lo, hi] of z the frame covers: its origin edge lies on `originZ`, it extends towards +z by H = 5d
// + 2m (model +y = world +z), scaled by f.scale about the origin edge, then moved by f.shift.
void Covered(float originZ, const rs7frets::FrameFit& f, float* lo, float* hi) {
    const float h = 5.0f * kD + 2.0f * kM;

    *lo = originZ + f.shift;
    *hi = originZ + f.shift + h * f.scale;
}
}  // namespace

TEST(frets_chordframe_normal_order_five_string_chord_is_exactly_stock) {
    rs7frets::FrameFit f{};

    CHECK(rs7frets::ChordFrame(kStringsNormal0, kStringsNormal1, kFloor, false, 5, &f));
    CHECK(Near(f.scale, 1.0f, 1e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
}

TEST(frets_chordframe_normal_order_string_6_chord_is_the_old_1_134) {
    rs7frets::FrameFit f{};

    CHECK(rs7frets::ChordFrame(kStringsNormal0, kStringsNormal1, kFloor, false, 6, &f));
    CHECK(Near(f.scale, (6.0f * kD + 2.0f * kM) / (5.0f * kD + 2.0f * kM), 2e-3f));
    CHECK(Near(f.scale, 1.134f, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
    float lo, hi;

    Covered(-5.99f, f, &lo, &hi);
    CHECK(Near(lo, -5.99f, 0.01f));
    CHECK(Near(hi, (4.0196f + kD) + kM, 0.03f));  // lane 6 + margin
}

TEST(frets_chordframe_reversed_order_floor_moved_five_string_chord_keeps_the_origin_on_the_floor) {
    // Reversed: string 0 at -4.02, string 6 at -5.63; the floor b = -5.99 (1.97 beyond string 0) is moved one spacing
    // out to -7.60. The frame's origin edge stays ON that floor (a frame translated back floated above the fret numbers
    // in game); only its far edge grows to string 5 + the stock margin: the old 1.134, no shift.
    rs7frets::FrameFit f{};

    CHECK(rs7frets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, true, 5, &f));
    CHECK(Near(f.scale, 1.134f, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
    float lo, hi;

    Covered(-5.99f - kD, f, &lo, &hi);
    CHECK(Near(lo, -5.99f - kD, 0.01f));
    CHECK(Near(hi, 4.0196f + kM, 0.03f));  // string 5 + the stock margin (the bug: only 0.36 beyond)
}

TEST(frets_chordframe_reversed_order_floor_moved_string_6_chord_reaches_lane_6) {
    rs7frets::FrameFit f{};

    CHECK(rs7frets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, true, 6, &f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
    float lo, hi;

    Covered(-5.99f - kD, f, &lo, &hi);
    CHECK(Near(lo, -4.0196f - kD - kM, 0.02f));  // lane 6 (-5.63) - margin = the moved floor
    CHECK(Near(hi, 4.0196f + kM, 0.03f));
}

TEST(frets_chordframe_reversed_order_without_a_moved_floor_and_string_7) {
    // fret_extend off (floor not moved): the origin edge stays at -5.99, a string-6 chord moves it out by one spacing.
    rs7frets::FrameFit f{};

    CHECK(rs7frets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, false, 6, &f));
    CHECK(Near(f.shift, -kD, 5e-3f));
    CHECK(Near(f.scale, 1.134f, 2e-3f));
    // String 7 (8-string guitar): two lanes beyond string 0.
    CHECK(rs7frets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, true, 7, &f));
    CHECK(Near(f.scale, (7.0f * kD + 2.0f * kM) / (5.0f * kD + 2.0f * kM), 2e-3f));
    CHECK(Near(f.shift, -kD, 5e-3f));  // the moved floor lies one spacing short of lane 7 - margin
}

TEST(frets_chordframe_rejects_degenerate_geometry) {
    rs7frets::FrameFit f{};

    f.scale = 7.0f;
    CHECK(!rs7frets::ChordFrame(kStringsNormal0, kStringsNormal0, kFloor, false, 5, &f));
    const float nearB[3] = {0.0f, 0.0f, 3.9f};  // the floor end inside the lanes: no margin

    CHECK(!rs7frets::ChordFrame(kStringsNormal0, kStringsNormal1, nearB, false, 5, &f));
    CHECK(f.scale == 7.0f);
}
