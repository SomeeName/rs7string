#include "features/nslot/nslot.h"
#include "model/eight.h"
#include "test.h"
#include <cstring>

using namespace rs7ns;

namespace {
constexpr uint32_t kBendCountOffset = kBendSize - sizeof(uint32_t);
constexpr uint32_t kLastBendValueByte = kBendCountOffset - 1;

// A stock C#5-style template: string 0 fret 4 (MIDI 44), finger 3; everything else unused.
void StockTemplate(uint8_t* t, const char* name = "C#5") {
    std::memset(t, 0, kTplSize);
    std::memset(t + 4, 0xFF, 6);
    std::memset(t + 0xA, 0xFF, 6);
    std::memset(t + 0x10, 0xFF, 24);
    t[4] = 4;
    t[0xA] = 3;
    const int32_t m = 44;

    std::memcpy(t + 0x10, &m, 4);
    strcpy_s(reinterpret_cast<char*>(t + 0x28), kTplSize - 0x28, name);
}

const int8_t* Frets(const uint8_t* t8) {
    return reinterpret_cast<const int8_t*>(t8 + kT8Frets);
}

const int8_t* Fingers(const uint8_t* t8) {
    return reinterpret_cast<const int8_t*>(t8 + kT8Fingers);
}

const int32_t* Midi(const uint8_t* t8) {
    return reinterpret_cast<const int32_t*>(t8 + kT8Midi);
}
}  // namespace

TEST(nslot_layout_constants) {
    CHECK_EQ(kT8Size, 0x80u);
    CHECK_EQ(kC8Size, 0x15A8u);
    CHECK_EQ(sizeof(Rs7NsEntry), 32u);  // the asm lookups step 32 bytes per entry
}

TEST(t8_keeps_the_stock_prefix_and_copies_strings_0_to_5) {
    uint8_t t[kTplSize], out[kT8Size];

    StockTemplate(t);
    const Col none[2];
    const int16_t tun[6] = {};

    BuildT8(t, none, tun, 6, 0, out);
    CHECK(std::memcmp(out, t, kTplSize) == 0);
    CHECK_EQ(Frets(out)[0], 4);
    CHECK_EQ(Fingers(out)[0], 3);
    CHECK_EQ(Midi(out)[0], 44);

    for (int s = 1; s < kSlots; ++s) {
        CHECK_EQ(Frets(out)[s], -1);
        CHECK_EQ(Midi(out)[s], -1);
    }

    CHECK_EQ(out[kT8Slots], 6);
    CHECK_EQ(out[kT8Used], 1);
}

TEST(t8_extended_columns_get_their_pitch_from_the_tuning) {
    uint8_t t[kTplSize], out[kT8Size];

    StockTemplate(t);
    const Col ext[2] = {{2, 1}, {-1, -1}};
    const int16_t tun[7] = {0, 0, 0, 0, 0, 0, -1};  // B lowered to A#

    BuildT8(t, ext, tun, 7, 0, out);
    CHECK_EQ(Frets(out)[6], 2);
    CHECK_EQ(Fingers(out)[6], 1);
    CHECK_EQ(Midi(out)[6], 35 - 1 + 2);
    CHECK_EQ(Frets(out)[7], -1);
    CHECK_EQ(out[kT8Slots], 7);
    CHECK_EQ(out[kT8Used], 0x41);
}

TEST(t8_ignores_columns_beyond_the_tuning_and_applies_the_capo_rule) {
    uint8_t t[kTplSize], out[kT8Size];

    StockTemplate(t);
    const Col ext[2] = {{3, 2}, {5, 4}};
    const int16_t tun[7] = {};

    BuildT8(t, ext, tun, 7, 3, out);  // capo 3: fret 3 becomes 0 (PostProcessNotes 0x55B4A2)
    CHECK_EQ(Frets(out)[6], 0);
    CHECK_EQ(Midi(out)[6], 38);  // the pitch keeps the capo (as stock midi[] does)
    CHECK_EQ(Fingers(out)[6], 2);
    CHECK_EQ(Frets(out)[7], -1);  // only 7 tuning entries
    const int16_t tun8[8] = {};

    BuildT8(t, ext, tun8, 8, 0xFF, out);  // 8 strings: not a mode the mod runs yet - no extended slots
    CHECK_EQ(Frets(out)[6], -1);
    CHECK_EQ(Frets(out)[7], -1);
    const Col big[2] = {{2, 6}, {-1, -1}};

    BuildT8(t, big, tun, 7, 0xFF, out);  // fingers above 4 (pinky) are dropped: 0x58C300 has five
    CHECK_EQ(Fingers(out)[6], -1);
}

TEST(name31_decodes_only_with_a_terminated_name) {
    uint8_t t[kTplSize];

    StockTemplate(t);
    Col c;

    CHECK(!DecodeName31(t, &c));
    t[0x28 + 31] = static_cast<uint8_t>(((1 + 1) << 5) | (2 + 1));
    CHECK(DecodeName31(t, &c));
    CHECK_EQ(c.fret, 2);
    CHECK_EQ(c.finger, 1);
    std::memset(t + 0x28, 'A', 31);  // a 31-character name: byte 31 is text
    CHECK(!DecodeName31(t, &c));
}

TEST(c8_copies_strings_0_to_5_and_converts_flagged_unpitched_slides) {
    static uint8_t cn[kCnSize], out[kC8Size];

    std::memset(cn, 0, sizeof cn);
    std::memset(cn + 0x930, 0xFF, 12);
    const uint32_t m0 = 0x2000;

    std::memcpy(cn, &m0, 4);
    cn[0x930] = 7;
    const uint32_t used = 2;

    std::memcpy(cn + 0x18 + kBendCountOffset, &used, 4);
    const float bends[3] = {1.5f, 1.0f, 0.0f};
    Tech t6;

    t6.mask = 0x8000000 | 0x400000;
    t6.slideUnpitchTo = 5;
    t6.bends = bends;
    t6.bendCount = 1;
    BuildC8(cn, &t6, nullptr, out);
    CHECK(std::memcmp(out, cn, kCnSize) == 0);
    const uint32_t* mask = reinterpret_cast<const uint32_t*>(out + kC8Mask);

    CHECK_EQ(mask[0], 0x2000u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo]), 7);
    CHECK_EQ(mask[6], 0x8000000u | 0x800u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 6]), 5);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideUnp + 6]), -1);
    uint32_t n;

    std::memcpy(&n, out + kC8Bends + kBendCountOffset, 4);
    CHECK_EQ(n, 2u);
    std::memcpy(&n, out + kC8Bends + 6 * kBendSize + kBendCountOffset, 4);
    CHECK_EQ(n, 1u);
    CHECK(std::memcmp(out + kC8Bends + 6 * kBendSize, bends, 12) == 0);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 7]), -1);
}

TEST(synthesized_mask_follows_fixup) {
    CHECK_EQ(SynthMask(0, -1, 0), 0u);
    CHECK_EQ(SynthMask(0, 0, 0), 4u);
    CHECK_EQ(SynthMask(0x40 | 8, 2, 0xFF), 0x40u | 0x20000u);
    CHECK_EQ(SynthMask(0x4000000, 3, 3), 0x4000000u | 4u);
}

TEST(t8_eight_optin_preserves_both_authored_columns) {
    const auto saved = g_rs7_eight;

    g_rs7_eight = 1;
    uint8_t t[kTplSize], out[kT8Size];

    StockTemplate(t);
    const Col ext[2] = {{2, 1}, {0, -1}};
    const int16_t tuning[8] = {0, 0, 0, 0, 0, 0, 0, -2};

    BuildT8(t, ext, tuning, 8, 0xFF, out);
    CHECK(std::memcmp(t, out, kTplSize) == 0);
    CHECK_EQ(Frets(out)[6], 2);
    CHECK_EQ(Frets(out)[7], 0);
    CHECK_EQ(Midi(out)[6], 37);
    CHECK_EQ(Midi(out)[7], 28);
    CHECK_EQ(out[kT8Used], 0xC1);
    g_rs7_eight = saved;
}

TEST(nslot_string_mask8_tests_the_c8_mask_of_strings_0_to_7) {
    uint8_t c8[kC8Size] = {};
    const uint32_t mask[kSlots] = {0x1, 0x2, 0x0, 0xFF, 0x80000000, 0x10, 0x3, 0xFFFFFFFF};
    uint32_t bits = 0xDEAD;

    std::memcpy(c8 + kC8Mask, mask, sizeof mask);
    CHECK(StringMask8(c8, 0, 0x1, &bits) && bits == 0x1);
    CHECK(StringMask8(c8, 2, 0xFFFFFFFF, &bits) && bits == 0);
    CHECK(StringMask8(c8, 6, 0x2, &bits) && bits == 0x2);
    CHECK(StringMask8(c8, 7, 0x80000000, &bits) && bits == 0x80000000);
    bits = 0xDEAD;
    CHECK(!StringMask8(c8, 8, 0x1, &bits) && bits == 0xDEAD);
}

TEST(t8_reused_output_clears_extra_columns_after_switching_to_six_strings) {
    uint8_t stock[kTplSize], out[kT8Size];

    StockTemplate(stock);
    const Col ext[2] = {{0, -1}, {2, 1}};
    const int16_t tuning[7] = {0, 0, 0, 0, 0, 0, -2};

    BuildT8(stock, ext, tuning, 7, 0xFF, out);
    CHECK_EQ(Midi(out)[6], 33);
    CHECK_EQ(out[kT8Used], 0x41);
    BuildT8(stock, ext, tuning, 6, 0xFF, out);
    CHECK_EQ(out[kT8Slots], 6);
    CHECK_EQ(out[kT8Used], 1);

    for (int s = 6; s < 8; ++s) {
        CHECK_EQ(Frets(out)[s], -1);
        CHECK_EQ(Fingers(out)[s], -1);
        CHECK_EQ(Midi(out)[s], -1);
    }
}

TEST(t8_invalid_extended_frets_do_not_create_used_strings) {
    uint8_t stock[kTplSize], out[kT8Size];

    StockTemplate(stock);

    for (int fret : {-2, -1, 31, 127}) {
        const Col ext[2] = {{fret, 1}, {-1, -1}};

        BuildT8(stock, ext, nullptr, 7, 0xFF, out);
        CHECK_EQ(Frets(out)[6], -1);
        CHECK_EQ(Midi(out)[6], -1);
        CHECK_EQ(out[kT8Used], 1);
    }
}

TEST(t8_missing_tuning_uses_standard_pitch_and_clamps_bad_fingers) {
    uint8_t stock[kTplSize], out[kT8Size];

    StockTemplate(stock);

    for (int finger : {-2, 5, 127}) {
        const Col ext[2] = {{30, finger}, {-1, -1}};

        BuildT8(stock, ext, nullptr, 7, 0xFF, out);
        CHECK_EQ(Frets(out)[6], 30);
        CHECK_EQ(Fingers(out)[6], -1);
        CHECK_EQ(Midi(out)[6], 65);
    }
}

TEST(c8_reused_output_clears_techniques_when_next_chord_has_no_extensions) {
    uint8_t stock[kCnSize] = {}, out[kC8Size];
    const uint32_t bend[3] = {0x3F800000, 0x3F000000, 0};
    Tech tech;

    tech.mask = 0x2000;
    tech.slideTo = 24;
    tech.vibrato = 100;
    tech.bends = bend;
    tech.bendCount = 1;
    BuildC8(stock, &tech, &tech, out);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(out + kC8Mask + 28), 0x2000u);
    BuildC8(stock, nullptr, nullptr, out);

    for (int s = 6; s < 8; ++s) {
        CHECK_EQ(*reinterpret_cast<const uint32_t*>(out + kC8Mask + 4 * s), 0u);
        CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + s]), -1);
        CHECK_EQ(static_cast<int8_t>(out[kC8SlideUnp + s]), -1);
        CHECK_EQ(*reinterpret_cast<const int16_t*>(out + kC8Vib + 2 * s), 0);

        for (uint32_t i = 0; i < kBendSize; ++i) {
            CHECK_EQ(out[kC8Bends + s * kBendSize + i], 0);
        }
    }
}

TEST(c8_bend_count_is_bounded_without_overwriting_the_next_string) {
    uint8_t stock[kCnSize] = {}, out[kC8Size];
    uint32_t bends[33 * 3];

    std::memset(bends, 0x5A, sizeof bends);
    Tech tech;

    tech.bends = bends;

    for (int count : {-1, 0, 32, 33}) {
        tech.bendCount = count;
        BuildC8(stock, &tech, nullptr, out);
        const uint8_t* b = out + kC8Bends + 6 * kBendSize;

        CHECK_EQ(*reinterpret_cast<const uint32_t*>(b + kBendCountOffset), count <= 0 ? 0u : 32u);
        CHECK_EQ(*reinterpret_cast<const uint32_t*>(b + kBendSize), 0u);
        if (count > 0) {
            CHECK_EQ(b[kLastBendValueByte], 0x5A);
        }
    }
}

TEST(c8_explicit_pitched_slide_prevents_unpitched_slide_conversion) {
    uint8_t stock[kCnSize] = {}, out[kC8Size];
    Tech tech;

    tech.mask = 0x8000000 | 0x400000;
    tech.slideTo = 7;
    tech.slideUnpitchTo = 12;
    BuildC8(stock, &tech, nullptr, out);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(out + kC8Mask + 24), 0x8400000u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 6]), 7);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideUnp + 6]), 12);
}

TEST(nslot_eval_reentry_clears_previous_chord_tails_when_new_data_is_absent) {
    uint8_t eval[0x300], t8[kT8Size], c8[kC8Size];

    std::memset(eval, 0x5A, sizeof eval);
    std::memset(t8, 0x11, sizeof t8);
    std::memset(c8, 0x22, sizeof c8);
    WriteEvalTails(eval, t8, c8);
    CHECK_EQ(eval[kEvFret67], 0x11);
    CHECK_EQ(eval[kEvMask67], 0x22);
    WriteEvalTails(eval, nullptr, nullptr);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(eval + kEvMidi67), -1);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(eval + kEvMask67), 0u);
    CHECK_EQ(static_cast<int8_t>(eval[kEvFret67]), -1);
    CHECK_EQ(static_cast<int8_t>(eval[kEvFinger67]), -1);
    CHECK_EQ(static_cast<int8_t>(eval[kEvSlide67]), -1);
    CHECK_EQ(static_cast<int8_t>(eval[kEvUnp67]), -1);
    CHECK_EQ(*reinterpret_cast<const int16_t*>(eval + kEvVib67), 0);
    CHECK_EQ(eval[kEvMidi67 - 1], 0x5A);
    CHECK_EQ(eval[kEvVib67 + 8], 0x5A);
}

TEST(nslot_double_stop_span_is_stock_for_an_unregistered_vector) {
    const uint32_t begin = 0x10000000;

    // Rs7Ns_TplRecord answered the stock record (vector not registered / id out of range): the stock loop.
    const FretSpan stock = DoubleStopSpan(begin, 3, begin + 3 * kTplSize);

    CHECK(stock.frets == begin + 3 * kTplSize + 4 && stock.count == 6);
    // A T8 record: its frets[8] over the native strings.
    const FretSpan t8 = DoubleStopSpan(begin, 3, 0x20000000 + 3 * kT8Size);

    CHECK(t8.frets == 0x20000000 + 3 * kT8Size + kT8Frets && t8.count == kNative);
}

TEST(nslot_double_stop_span_through_the_registry) {
    const uint32_t begin = 0x10000000, t8 = 0x20000000;
    const Rs7NsEntry saved = g_rs7_ns[5];
    auto span = [](uint32_t b, uint32_t id) {
        return DoubleStopSpan(b, id, Rs7Ns_TplRecord(b, id));
    };
    auto isStock = [](FretSpan s, uint32_t b, uint32_t id) {
        return s.frets == b + id * kTplSize + 4 && s.count == 6;
    };

    g_rs7_ns[5] = Rs7NsEntry{begin, 4, t8, 0, 0, 0, 0x5000, 0};
    const FretSpan last = span(begin, 3);

    CHECK(last.frets == t8 + 3 * kT8Size + kT8Frets && last.count == kNative);
    CHECK(isStock(span(begin, 4), begin, 4));                  // id == tplCount
    CHECK(isStock(span(begin + 0x100, 0), begin + 0x100, 0));  // a vector not in the registry
    g_rs7_ns[5].tplCount = 0;
    CHECK(isStock(span(begin, 0), begin, 0));  // tplCount 0
    g_rs7_ns[5] = saved;
}
