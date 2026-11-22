#include "features/frets/frets.h"
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
    rsxrfrets::Extend x{};

    CHECK(rsxrfrets::Compute(a0, a1, mid, 4.61f, 1, &x));
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
    rsxrfrets::Extend x{};

    CHECK(rsxrfrets::Compute(a0, a1, mid, L, 1, &x));
    const float lo = mid[2] + x.shift[2] - x.scale * L, hi = mid[2] + x.shift[2] + x.scale * L;
    const float s6 = -4.0196f - 1.6078f, s5 = -4.0196f + 5.0f * 1.6078f;

    CHECK(Near(hi, mid[2] + L, 1e-3f));            // string-5 end unchanged
    CHECK(Near(s6 - lo, mid[2] + L - s5, 1e-3f));  // margin beyond string 6 = the stock 0.86
    CHECK(Near(mid[2] + L - s5, 0.8614f, 2e-3f));
}

// 8-string guitar: the near end grows by two spacings (the 8th lane), the string-5 end stays.
TEST(frets_extend_eight_strings_reaches_two_spacings_beyond_string_0) {
    const float a0[3] = {0.0f, -53.653f, 4.0196f}, a1[3] = {0.0f, -53.653f, 4.0196f - 1.6078f};
    const float mid[3] = {0.0f, -53.653f, 0.271f};
    const float c = 0.271f, L = 4.61f;
    rsxrfrets::Extend x{};

    CHECK(rsxrfrets::Compute(a0, a1, mid, L, 2, &x));
    const float lo = c + x.shift[2] - x.scale * L, hi = c + x.shift[2] + x.scale * L;

    CHECK(Near(lo, c - L, 1e-3f));
    CHECK(Near(hi, c + L + 2.0f * 1.6078f, 1e-3f));
    CHECK(!rsxrfrets::Compute(a0, a1, mid, L, 0, &x) && !rsxrfrets::Compute(a0, a1, mid, L, 3, &x));
}

// 6-string bass: the stock bar spans strings 0..3 (G); the low B lane beyond string 0 and the high C lane beyond the G
// each add one spacing, the stock margins kept at both ends.
TEST(frets_extend_six_string_bass_reaches_one_spacing_beyond_the_g) {
    const float a0[3] = {0.0f, 0.0f, 3.0f}, a1[3] = {0.0f, 0.0f, 1.0f};
    const float mid[3] = {0.0f, 0.0f, 0.0f};
    const float L = 4.0f;
    rsxrfrets::Extend x{};

    CHECK(rsxrfrets::Compute(a0, a1, mid, L, 1, &x, 3, 1));
    const float lo = x.shift[2] - x.scale * L, hi = x.shift[2] + x.scale * L;

    CHECK(Near(hi, L + 2.0f, 1e-3f));   // one spacing (2) beyond the stock string-0 end
    CHECK(Near(lo, -L - 2.0f, 1e-3f));  // one spacing beyond the stock G end
    CHECK(rsxrfrets::Compute(a0, a1, mid, L, 1, &x, 3, 0));
    CHECK(Near(x.shift[2] - x.scale * L, -L, 1e-3f));  // a 5-string bass keeps the G end
    CHECK(!rsxrfrets::Compute(a0, a1, mid, L, 1, &x, 3, 2));
    CHECK(!rsxrfrets::Compute(a0, a1, mid, L, 1, &x, 0, 0) && !rsxrfrets::Compute(a0, a1, mid, L, 1, &x, 6, 0));
}

TEST(frets_extend_rejects_degenerate_input) {
    const float a[3] = {1.0f, 2.0f, 3.0f};
    rsxrfrets::Extend x{};

    x.scale = 7.0f;
    CHECK(!rsxrfrets::Compute(a, a, a, 4.61f, 1, &x));
    const float b[3] = {1.0f, 2.0f, 4.0f};

    CHECK(!rsxrfrets::Compute(a, b, a, 0.0f, 1, &x));
    CHECK(x.scale == 7.0f);  // untouched
}

// The highway floor: the floor (fret-wire bottom end b) on string 6's side of string 0 (inverted strings) moves one
// spacing out.
TEST(frets_floor_shift_only_on_string_6_side) {
    const float w0[3] = {0.0f, 0.0f, 4.0196f}, w1[3] = {0.0f, 0.0f, 4.0196f - 1.6078f};
    float out[3] = {9.0f, 9.0f, 9.0f};
    const float bInv[3] = {1.0f, -53.6f, 5.99f};  // beyond string 0, string 6's side: inverted

    CHECK(rsxrfrets::FloorShift(w0, w1, bInv, 1, out));
    CHECK(Near(out[0], 0.0f) && Near(out[1], 0.0f) && Near(out[2], 1.6078f));
    // New floor 1.97 beyond string 6 (5.6274), like the stock floor beyond the outer string.
    CHECK(Near(bInv[2] + out[2] - (2.0f * w0[2] - w1[2]), 5.99f - w0[2], 1e-3f));
    const float bNorm[3] = {1.0f, -53.6f, -5.99f};  // beyond string 5: normal order, stock

    out[2] = 9.0f;
    CHECK(!rsxrfrets::FloorShift(w0, w1, bNorm, 1, out));
    CHECK(out[2] == 9.0f);
    // Mirrored (string 6 towards -z) works the same way.
    const float m0[3] = {0.0f, 0.0f, -4.0f}, m1[3] = {0.0f, 0.0f, -2.4f}, bm[3] = {0.0f, 0.0f, -6.0f};

    CHECK(rsxrfrets::FloorShift(m0, m1, bm, 1, out) && Near(out[2], -1.6f));
    CHECK(!rsxrfrets::FloorShift(w0, w0, bInv, 1, out));  // degenerate spacing
    // 8-string guitar: two spacings out, so lane 7 lies inside the floor like lane 6 on a 7-string.
    CHECK(rsxrfrets::FloorShift(w0, w1, bInv, 2, out) && Near(out[2], 2.0f * 1.6078f));
    CHECK(!rsxrfrets::FloorShift(w0, w1, bInv, 0, out) && !rsxrfrets::FloorShift(w0, w1, bInv, 3, out));
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
void Covered(float originZ, const rsxrfrets::FrameFit& f, float* lo, float* hi) {
    const float h = 5.0f * kD + 2.0f * kM;

    *lo = originZ + f.shift;
    *hi = originZ + f.shift + h * f.scale;
}
}  // namespace

TEST(frets_chordframe_normal_order_five_string_chord_is_exactly_stock) {
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(kStringsNormal0, kStringsNormal1, kFloor, 0, 5, &f));
    CHECK(Near(f.scale, 1.0f, 1e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
}

TEST(frets_chordframe_normal_order_string_6_chord_is_the_old_1_134) {
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(kStringsNormal0, kStringsNormal1, kFloor, 0, 6, &f));
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
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, 1, 5, &f));
    CHECK(Near(f.scale, 1.134f, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
    float lo, hi;

    Covered(-5.99f - kD, f, &lo, &hi);
    CHECK(Near(lo, -5.99f - kD, 0.01f));
    CHECK(Near(hi, 4.0196f + kM, 0.03f));  // string 5 + the stock margin (the bug: only 0.36 beyond)
}

TEST(frets_chordframe_reversed_order_floor_moved_string_6_chord_reaches_lane_6) {
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, 1, 6, &f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
    float lo, hi;

    Covered(-5.99f - kD, f, &lo, &hi);
    CHECK(Near(lo, -4.0196f - kD - kM, 0.02f));  // lane 6 (-5.63) - margin = the moved floor
    CHECK(Near(hi, 4.0196f + kM, 0.03f));
}

TEST(frets_chordframe_reversed_order_without_a_moved_floor_and_string_7) {
    // fret_extend off (floor not moved): the origin edge stays at -5.99, a string-6 chord moves it out by one spacing.
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, 0, 6, &f));
    CHECK(Near(f.shift, -kD, 5e-3f));
    CHECK(Near(f.scale, 1.134f, 2e-3f));
    // String 7 (8-string guitar): two lanes beyond string 0.
    CHECK(rsxrfrets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, 1, 7, &f));
    CHECK(Near(f.scale, (7.0f * kD + 2.0f * kM) / (5.0f * kD + 2.0f * kM), 2e-3f));
    CHECK(Near(f.shift, -kD, 5e-3f));  // a floor moved one spacing lies one spacing short of lane 7 - margin
    // An 8-string floor moved two spacings already covers lane 7: the origin edge stays.
    CHECK(rsxrfrets::ChordFrame(kStringsRev0, kStringsRev1, kFloor, 2, 7, &f));
    CHECK(Near(f.scale, (7.0f * kD + 2.0f * kM) / (5.0f * kD + 2.0f * kM), 2e-3f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
}

TEST(frets_chordframe_rejects_degenerate_geometry) {
    rsxrfrets::FrameFit f{};

    f.scale = 7.0f;
    CHECK(!rsxrfrets::ChordFrame(kStringsNormal0, kStringsNormal0, kFloor, 0, 5, &f));
    const float nearB[3] = {0.0f, 0.0f, 3.9f};  // the floor end inside the lanes: no margin

    CHECK(!rsxrfrets::ChordFrame(kStringsNormal0, kStringsNormal1, nearB, 0, 5, &f));
    CHECK(f.scale == 7.0f);
}

// [neck] spacing_bass6 = 1.608 (k 0.6 on the stock 2.68): the bars are the stock 6-string bass result scaled by k along
// the string axis, so their margin beyond the low B / high C is the stock one times k, not the wide stock mesh's.
TEST(frets_spacing_scales_the_extended_bar_with_the_strings) {
    const float d = 2.68f, k = 0.6f, L = 4.61f;
    const float a0[3] = {0.0f, -53.653f, 1.5f * d}, a1[3] = {0.0f, -53.653f, 0.5f * d};
    const float mid[3] = {0.0f, -53.653f, 0.271f};
    rsxrfrets::Extend stock{}, narrow{};

    CHECK(rsxrfrets::Compute(a0, a1, mid, L, 1, &stock, 3, 1));

    const float b0[3] = {0.0f, -53.653f, 1.5f * d * k}, b1[3] = {0.0f, -53.653f, 0.5f * d * k};
    float m[3] = {mid[0], mid[1], mid[2]}, moved[3] = {};

    rsxrfrets::ScaleBarMid(b0, b1, k, m, moved);
    CHECK(Near(m[0], 0.0f) && Near(m[1], -53.653f) && Near(m[2], 0.271f * k));
    CHECK(Near(moved[2], 0.271f * (k - 1.0f)));
    CHECK(rsxrfrets::Compute(b0, b1, m, L * k, 1, &narrow, 3, 1));
    // Final mesh scale and bar centre: k times the stock ones.
    CHECK(Near(narrow.scale * k, stock.scale * k));
    CHECK(Near(mid[2] + moved[2] + narrow.shift[2], k * (mid[2] + stock.shift[2])));
}

TEST(frets_chordframe_extended_bass_covers_the_low_b_and_the_high_c) {
    // Reversed bass lanes 0-3 at -1.5d .. +1.5d, the stock floor kM beyond string 0: the stock frame spans 3d + 2m.
    const float b0[3] = {0.0f, 0.0f, -1.5f * kD}, b1[3] = {0.0f, 0.0f, -0.5f * kD};
    const float floor[3] = {0.0f, 0.0f, -1.5f * kD - kM};
    const float h = 3.0f * kD + 2.0f * kM;
    rsxrfrets::FrameFit f{};

    CHECK(rsxrfrets::ChordFrame(b0, b1, floor, 0, 5, &f, 3, 0));  // a 4-string bass: exactly the stock frame
    CHECK(Near(f.scale, 1.0f, 1e-3f));
    CHECK(Near(f.shift, 0.0f, 1e-3f));
    // 5-string: floor moved one spacing for the low B; the far edge stays at the G + margin.
    CHECK(rsxrfrets::ChordFrame(b0, b1, floor, 1, 6, &f, 3, 0));
    CHECK(Near(f.scale, (4.0f * kD + 2.0f * kM) / h, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
    // 6-string: the high C one more lane beyond the G.
    CHECK(rsxrfrets::ChordFrame(b0, b1, floor, 1, 6, &f, 3, 1));
    CHECK(Near(f.scale, (5.0f * kD + 2.0f * kM) / h, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
    CHECK(!rsxrfrets::ChordFrame(b0, b1, floor, 1, 6, &f, 0, 0) &&
          !rsxrfrets::ChordFrame(b0, b1, floor, 1, 6, &f, 6, 0));
    // Normal order (the floor kM beyond the G): the floor moves one spacing for the high C, the frame keeps its
    // origin edge on it and grows to the low B + margin.
    const float nfloor[3] = {0.0f, 0.0f, 1.5f * kD + kM};

    CHECK(rsxrfrets::ChordFrame(b0, b1, nfloor, 1, 6, &f, 3, 1));
    CHECK(Near(f.scale, (5.0f * kD + 2.0f * kM) / h, 2e-3f));
    CHECK(Near(f.shift, 0.0f, 5e-3f));
    CHECK(!rsxrfrets::ChordFrame(b0, b1, floor, 1, 6, &f, 3, 2));
}

TEST(frets_floor_move_for_the_high_c_of_a_six_string_bass) {
    // Bass lanes 0-3 at -1.5d .. +1.5d (string 0 at -1.5d), the high C beyond the G at +2.5d.
    const float b0[3] = {0.0f, 0.0f, -1.5f * kD}, b1[3] = {0.0f, 0.0f, -0.5f * kD};
    const float beyondG[3] = {0.0f, 0.0f, 1.5f * kD + kM}, beyondE[3] = {0.0f, 0.0f, -1.5f * kD - kM};
    float out[3] = {9.0f, 9.0f, 9.0f};

    // 6-string bass, floor beyond the G: one spacing further out, so the high C lane lies kM above it.
    CHECK(rsxrfrets::FloorMove(b0, b1, beyondG, 1, 3, 1, out) == 1);
    CHECK(Near(out[2], kD) && Near(beyondG[2] + out[2] - 2.5f * kD, kM, 1e-3f));
    // 5-string bass there: no lane beyond the G, the floor stays.
    out[2] = 9.0f;
    CHECK(rsxrfrets::FloorMove(b0, b1, beyondG, 1, 3, 0, out) == 0 && out[2] == 9.0f);
    // Floor beyond the E (reversed): the low B side moves, as FloorShift.
    CHECK(rsxrfrets::FloorMove(b0, b1, beyondE, 1, 3, 1, out) == 1 && Near(out[2], -kD));
    // 4-string bass: nothing.
    CHECK(rsxrfrets::FloorMove(b0, b1, beyondE, 0, 3, 0, out) == 0);
}

TEST(frets_glow_end_moves_only_the_end_past_string_0) {
    // String 0 at z = 0, string 1 at z = -d: the extra strings lie at +d, +2d.
    const float w0[3] = {0.0f, 0.0f, 0.0f}, w1[3] = {0.0f, 0.0f, -kD};
    float nearEnd[3] = {0.0f, 1.0f, kM}, farEnd[3] = {0.0f, 1.0f, -5.0f * kD - kM};

    // 8-string guitar: the end beyond string 0 moves two spacings out, past the low F#; the other end stays.
    CHECK(rsxrfrets::GlowEnd(w0, w1, 2, nearEnd) && Near(nearEnd[2], 2.0f * kD + kM) && Near(nearEnd[1], 1.0f));
    CHECK(!rsxrfrets::GlowEnd(w0, w1, 2, farEnd) && Near(farEnd[2], -5.0f * kD - kM));
    // A stock instrument (no extra lanes) or a degenerate count: untouched.
    float p[3] = {0.0f, 0.0f, kM};

    CHECK(!rsxrfrets::GlowEnd(w0, w1, 0, p) && !rsxrfrets::GlowEnd(w0, w1, 3, p) && Near(p[2], kM));
}

TEST(frets_glow_stretch_spans_all_strings) {
    CHECK(Near(rsxrfrets::GlowStretch(7, 1.608f, 0.0f), 1.4f));     // 8-string guitar
    CHECK(Near(rsxrfrets::GlowStretch(4, 2.68f, 0.0f), 4.0f / 3));  // 5-string bass
    CHECK(Near(rsxrfrets::GlowStretch(5, 2.68f, 0.0f), 5.0f / 3));  // 6-string bass
    CHECK(Near(rsxrfrets::GlowStretch(4, 2.01f, 0.0f), 1.0f));      // 5-string bass as wide as a 4-string: stock
    CHECK(Near(rsxrfrets::GlowStretch(6, 1.0f, 0.0f), 1.0f) && Near(rsxrfrets::GlowStretch(9, 4.0f, 0.0f), 3.0f));
    // With the stock margin m beyond the outer strings (#283): 8 strings of 1.608 and m 1.97 -> (7d + 2m) / (5d + 2m).
    CHECK(Near(rsxrfrets::GlowStretch(7, 1.608f, 1.97f), (7 * 1.608f + 3.94f) / (8.04f + 3.94f)));
    CHECK(Near(rsxrfrets::GlowStretch(5, 1.608f, 1.97f), 1.0f));  // a 6-string guitar: stock
}

// #283: on the floor's side the glow end goes exactly to the floor, whether the floor shift already moved it or not.
TEST(frets_glow_end_at_floor_is_idempotent) {
    const float d = 1.6079f, m = 1.9704f, r = m / d;
    const float w0[3] = {0.0f, 1.0f, 0.0f}, w1[3] = {0.0f, 1.0f, -d};
    float unmoved[3] = {0.0f, 1.0f, m}, moved[3] = {0.0f, 1.0f, m + 2.0f * d}, far[3] = {0.0f, 1.0f, -5.0f * d - m};

    // 8-string: two lanes past the stock margin, from either starting point; the far end stays.
    CHECK(rsxrfrets::GlowEndAt(w0, w1, r + 2.0f, unmoved) && Near(unmoved[2], m + 2.0f * d, 1e-3f) &&
          Near(unmoved[1], 1.0f));
    CHECK(!rsxrfrets::GlowEndAt(w0, w1, r + 2.0f, moved) && Near(moved[2], m + 2.0f * d, 1e-3f));
    CHECK(!rsxrfrets::GlowEndAt(w0, w1, r + 2.0f, far) && Near(far[2], -5.0f * d - m));
    CHECK(!rsxrfrets::GlowEndAt(w0, w1, r + 2.0f, unmoved));  // a second call changes nothing

    // A world scale applies to the margin as well (it is in spacings).
    const float s0[3] = {0.0f, 0.0f, 0.0f}, s1[3] = {0.0f, 0.0f, -2.0f * d};
    float scaled[3] = {0.0f, 0.0f, 2.0f * m};

    CHECK(rsxrfrets::GlowEndAt(s0, s1, r + 1.0f, scaled) && Near(scaled[2], 2.0f * (m + d), 1e-3f));

    // Degenerate: no spacing, a non-finite or out-of-range target.
    float p[3] = {0.0f, 1.0f, m};

    CHECK(!rsxrfrets::GlowEndAt(w0, w0, r + 2.0f, p) && !rsxrfrets::GlowEndAt(w0, w1, 0.0f, p) &&
          !rsxrfrets::GlowEndAt(w0, w1, 9.0f, p) && Near(p[2], m));
}
