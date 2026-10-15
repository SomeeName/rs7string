#include "core/eight.h"
#include "core/nslot.h"
#include "test.h"
#include <cstring>

using namespace rs7ns;

namespace {
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
    std::strcpy(reinterpret_cast<char*>(t + 0x28), name);
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

    std::memcpy(cn + 0x18 + 0x180, &used, 4);
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

    std::memcpy(&n, out + kC8Bends + 0x180, 4);
    CHECK_EQ(n, 2u);
    std::memcpy(&n, out + kC8Bends + 6 * kBendSize + 0x180, 4);
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
