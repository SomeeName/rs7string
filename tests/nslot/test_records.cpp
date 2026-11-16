// Edge cases of the N-slot record builders (features/nslot/records.cpp) not covered by test_nslot.cpp.
#include "features/nslot/nslot.h"
#include "model/tuning.h"
#include "test.h"
#include <cstring>

using namespace rs7ns;

namespace {
// A template with all six stock strings unused and the name "X".
void EmptyTemplate(uint8_t* t) {
    std::memset(t, 0, kTplSize);
    std::memset(t + 4, 0xFF, 6);
    std::memset(t + 0xA, 0xFF, 6);
    std::memset(t + 0x10, 0xFF, 24);
    t[0x28] = 'X';
}

int8_t FretAt(const uint8_t* t8, int s) {
    return static_cast<int8_t>(t8[kT8Frets + s]);
}

int8_t FingerAt(const uint8_t* t8, int s) {
    return static_cast<int8_t>(t8[kT8Fingers + s]);
}

int32_t MidiAt(const uint8_t* t8, int s) {
    int32_t m = 0;

    std::memcpy(&m, t8 + kT8Midi + 4 * s, 4);

    return m;
}

uint32_t MaskAt(const uint8_t* c8, int s) {
    uint32_t m = 0;

    std::memcpy(&m, c8 + kC8Mask + 4 * s, 4);

    return m;
}
}  // namespace

// Catches: a string count outside 7..8 (corrupt tuning vector) writing extended columns or a slot count the widened
// loops would trust (9+ would index past the eight-slot arrays).
TEST(records_t8_slot_count_and_extended_columns_per_string_count) {
    uint8_t t[kTplSize], out[kT8Size];
    const Col ext[2] = {{2, 1}, {3, 2}};
    const int16_t tun[8] = {};

    EmptyTemplate(t);

    for (int n : {-1, 0, 4, 5, 6, 7, 8, 9, 100}) {
        BuildT8(t, ext, tun, n, 0xFF, out);
        const bool seven = n == 7 || n == 8, eight = n == 8;

        CHECK_EQ(out[kT8Slots], static_cast<uint8_t>(seven ? n : 6));
        CHECK_EQ(FretAt(out, 6), seven ? 2 : -1);
        CHECK_EQ(FretAt(out, 7), eight ? 3 : -1);
        CHECK_EQ(out[kT8Used], static_cast<uint8_t>((seven ? 0x40 : 0) | (eight ? 0x80 : 0)));
    }
}

// Catches: fret 0 (an open extended string) treated as "not in the chord", and the open string losing its pitch.
TEST(records_t8_open_extended_strings_are_used) {
    uint8_t t[kTplSize], out[kT8Size];
    const Col ext[2] = {{0, -1}, {0, -1}};

    EmptyTemplate(t);
    BuildT8(t, ext, nullptr, 8, 0xFF, out);
    CHECK_EQ(FretAt(out, 6), 0);
    CHECK_EQ(FretAt(out, 7), 0);
    CHECK_EQ(MidiAt(out, 6), rs7::kGuitarOpenMidi[6]);
    CHECK_EQ(MidiAt(out, 7), rs7::kGuitarOpenMidi[7]);
    CHECK_EQ(out[kT8Used], 0xC0);
}

// Catches: the eighth string reading the seventh string's tuning entry or open pitch (index off by one).
TEST(records_t8_eighth_string_pitch_uses_its_own_tuning_entry) {
    uint8_t t[kTplSize], out[kT8Size];
    const Col ext[2] = {{1, 0}, {5, 2}};
    const int16_t tun[8] = {0, 0, 0, 0, 0, 0, 3, -4};

    EmptyTemplate(t);
    BuildT8(t, ext, tun, 8, 0xFF, out);
    CHECK_EQ(MidiAt(out, 6), rs7::kGuitarOpenMidi[6] + 3 + 1);
    CHECK_EQ(MidiAt(out, 7), rs7::kGuitarOpenMidi[7] - 4 + 5);
    CHECK_EQ(FingerAt(out, 6), 0);
    CHECK_EQ(FingerAt(out, 7), 2);
}

// Catches: the capo rule zeroing frets when there is no capo (0xFF), or only applied to string 6.
TEST(records_t8_capo_rule_on_both_extended_strings) {
    uint8_t t[kTplSize], out[kT8Size];
    const Col ext[2] = {{2, 1}, {2, 1}};
    const int16_t tun[8] = {};

    EmptyTemplate(t);
    BuildT8(t, ext, tun, 8, 2, out);
    CHECK_EQ(FretAt(out, 6), 0);
    CHECK_EQ(FretAt(out, 7), 0);
    CHECK_EQ(MidiAt(out, 7), rs7::kGuitarOpenMidi[7] + 2);  // the pitch keeps the authored fret
    CHECK_EQ(out[kT8Used], 0xC0);                           // a zeroed fret is still a used string
    BuildT8(t, ext, tun, 8, 0xFF, out);
    CHECK_EQ(FretAt(out, 6), 2);
    CHECK_EQ(FretAt(out, 7), 2);
}

// Catches: the used mask built from the extended columns only (stock strings missing), or from fingers / midi.
TEST(records_t8_used_mask_covers_all_eight_strings) {
    uint8_t t[kTplSize], out[kT8Size];
    const Col ext[2] = {{7, 1}, {7, 1}};
    const int16_t tun[8] = {};

    EmptyTemplate(t);

    for (int s = 0; s < 6; ++s) {
        t[4 + s] = static_cast<uint8_t>(s);
    }

    BuildT8(t, ext, tun, 8, 0xFF, out);
    CHECK_EQ(out[kT8Used], 0xFF);
    t[4 + 2] = 0xFF;    // string 2 not in the chord
    t[0xA + 3] = 0xFF;  // string 3 without a finger: still used
    BuildT8(t, ext, tun, 6, 0xFF, out);
    CHECK_EQ(out[kT8Used], 0x3B);
    CHECK_EQ(out[0x7A], 0);  // +7A..+7F stay zero
    CHECK_EQ(out[0x7F], 0);
}

// Catches: an off-by-one in the old-format name check (a 30-character name is terminated at name[30]) and the
// finger / fret bit split of name[31].
TEST(records_name31_bit_fields_and_terminator_bounds) {
    uint8_t t[kTplSize];
    Col c;

    EmptyTemplate(t);
    std::memset(t + 0x28, 'A', 30);
    t[0x28 + 30] = 0;
    t[0x28 + 31] = 0x1F;  // fret 30, finger -1
    CHECK(DecodeName31(t, &c));
    CHECK_EQ(c.fret, 30);
    CHECK_EQ(c.finger, -1);
    t[0x28 + 31] = static_cast<uint8_t>((7 << 5) | 1);  // fret 0, finger 6 (decoded as is; BuildT8 drops it)
    CHECK(DecodeName31(t, &c));
    CHECK_EQ(c.fret, 0);
    CHECK_EQ(c.finger, 6);
    c.fret = 99;
    c.finger = 99;
    t[0x28 + 31] = static_cast<uint8_t>(3 << 5);  // fret field 0: no string-6 column, *out untouched
    CHECK(!DecodeName31(t, &c));
    CHECK_EQ(c.fret, 99);
    CHECK_EQ(c.finger, 99);
}

// Catches: the eighth string's technique written into slot 6, or a missing tech6 leaving slot 6 with garbage.
TEST(records_c8_eighth_string_only) {
    static uint8_t cn[kCnSize], out[kC8Size];
    Tech t7;

    std::memset(cn, 0, sizeof cn);
    std::memset(out, 0xCC, sizeof out);
    t7.mask = 0x2000;
    t7.slideTo = 9;
    t7.vibrato = 80;
    BuildC8(cn, nullptr, &t7, out);
    CHECK_EQ(MaskAt(out, 6), 0u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 6]), -1);
    CHECK_EQ(MaskAt(out, 7), 0x2000u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 7]), 9);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideUnp + 7]), -1);
    CHECK_EQ(*reinterpret_cast<const int16_t*>(out + kC8Vib + 2 * 7), 80);
}

// Catches: a bend count with no bend data recorded as that many zeroed bends.
TEST(records_c8_bend_count_without_data_records_no_bends) {
    static uint8_t cn[kCnSize], out[kC8Size];
    Tech t7;

    std::memset(cn, 0, sizeof cn);
    t7.bendCount = 5;
    BuildC8(cn, nullptr, &t7, out);
    uint32_t used = 0xFFFFFFFF;

    std::memcpy(&used, out + kC8Bends + 7 * kBendSize + 0x180, 4);
    CHECK_EQ(used, 0u);
}

// Catches: the stock columns copied with the wrong stride (bends 0x184, vibrato 2, slides 1 byte per string).
TEST(records_c8_copies_each_stock_string_at_its_own_offset) {
    static uint8_t cn[kCnSize], out[kC8Size];

    std::memset(cn, 0, sizeof cn);

    for (int s = 0; s < 6; ++s) {
        const uint32_t m = 0x100u << s;
        const int16_t v = static_cast<int16_t>(10 * (s + 1));

        std::memcpy(cn + 4 * s, &m, 4);
        cn[0x930 + s] = static_cast<uint8_t>(s + 1);
        cn[0x936 + s] = static_cast<uint8_t>(s + 11);
        std::memcpy(cn + 0x93C + 2 * s, &v, 2);
        cn[0x18 + s * kBendSize] = static_cast<uint8_t>(0xA0 + s);               // first bend byte
        cn[0x18 + s * kBendSize + kBendSize - 4] = static_cast<uint8_t>(s + 1);  // usedCount
    }

    BuildC8(cn, nullptr, nullptr, out);

    for (int s = 0; s < 6; ++s) {
        CHECK_EQ(MaskAt(out, s), 0x100u << s);
        CHECK_EQ(out[kC8SlideTo + s], s + 1);
        CHECK_EQ(out[kC8SlideUnp + s], s + 11);
        CHECK_EQ(*reinterpret_cast<const int16_t*>(out + kC8Vib + 2 * s), 10 * (s + 1));
        CHECK_EQ(out[kC8Bends + s * kBendSize], 0xA0 + s);
        CHECK_EQ(out[kC8Bends + s * kBendSize + kBendSize - 4], s + 1);
    }
}

// Catches: the unpitched-slide fixup firing without its 0x8000000 flag or without an unpitched target.
TEST(records_c8_unpitched_slide_fixup_needs_flag_and_target) {
    static uint8_t cn[kCnSize], out[kC8Size];
    Tech t;

    std::memset(cn, 0, sizeof cn);
    t.mask = 0x400000;  // unpitched slide, no 0x8000000
    t.slideUnpitchTo = 5;
    BuildC8(cn, &t, nullptr, out);
    CHECK_EQ(MaskAt(out, 6), 0x400000u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 6]), -1);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideUnp + 6]), 5);
    t.mask = 0x8000000 | 0x400000;  // flagged, but no unpitched target
    t.slideUnpitchTo = -1;
    BuildC8(cn, &t, nullptr, out);
    CHECK_EQ(MaskAt(out, 6), 0x8400000u);
    CHECK_EQ(static_cast<int8_t>(out[kC8SlideTo + 6]), -1);
}

// Catches: "no capo" (0xFF) treated as a capo at fret 255, and note bits other than the three kept leaking through.
TEST(records_synth_mask_filters_bits_and_no_capo) {
    CHECK_EQ(SynthMask(0, 255, 0xFF), 0u);
    CHECK_EQ(SynthMask(0xFFFFFFFF, 5, 0xFF), 0x40u | 0x20000u | 0x4000000u);
    CHECK_EQ(SynthMask(0xFFFFFFFF, -1, 0), 0u);  // not sounding: nothing, even with every bit set
    CHECK_EQ(SynthMask(0x2000 | 0x800, 4, 0xFF), 0u);
    CHECK_EQ(SynthMask(0, 7, 7), 4u);
}

// Catches: one half of the eval tails left stale when the record has only a T8 or only a C8.
TEST(records_eval_tails_from_one_record_clear_the_other_half) {
    static uint8_t eval[0xC28], t8[kT8Size], c8[kC8Size];
    const int32_t midi[2] = {36, 31};

    std::memset(eval, 0x5A, sizeof eval);
    std::memset(t8, 0, sizeof t8);
    std::memcpy(t8 + kT8Midi + 6 * 4, midi, 8);
    t8[kT8Frets + 6] = 1;
    t8[kT8Frets + 7] = 2;
    WriteEvalTails(eval, t8, nullptr);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(eval + kEvMidi67), 36);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(eval + kEvMidi67 + 4), 31);
    CHECK_EQ(eval[kEvFret67 + 1], 2);
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(eval + kEvMask67 + 4), 0u);
    CHECK_EQ(static_cast<int8_t>(eval[kEvSlide67 + 1]), -1);
    std::memset(eval, 0x5A, sizeof eval);
    std::memset(c8, 0, sizeof c8);
    c8[kC8Mask + 7 * 4] = 0x40;
    WriteEvalTails(eval, nullptr, c8);
    CHECK_EQ(eval[kEvMask67 + 4], 0x40);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(eval + kEvMidi67 + 4), -1);
    CHECK_EQ(static_cast<int8_t>(eval[kEvFret67 + 1]), -1);
    CHECK_EQ(static_cast<int8_t>(eval[kEvFinger67 + 1]), -1);
    CHECK_EQ(eval[kEvChild67], 0x5A);  // the child slots are not part of the tails
}

// Catches: the first template (id 0, record == begin) not recognised as the stock record.
TEST(records_double_stop_span_id_zero) {
    const FretSpan s = DoubleStopSpan(0x10000000, 0, 0x10000000);

    CHECK(s.frets == 0x10000004 && s.count == 6);
}
