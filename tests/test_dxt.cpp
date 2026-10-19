#include "platform/dxt.h"
#include "test.h"
#include <cstring>

namespace {
// One DXT1 colour block: c0, c1 (little endian), then 4 index bytes (row y in byte y, pixel x in bits 2x).
void Colour(uint8_t* b, uint16_t c0, uint16_t c1, const uint8_t idx[4]) {
    b[0] = uint8_t(c0);
    b[1] = uint8_t(c0 >> 8);
    b[2] = uint8_t(c1);
    b[3] = uint8_t(c1 >> 8);
    std::memcpy(b + 4, idx, 4);
}

// Index byte for one row: pixel 0..3 = i0..i3.
uint8_t Row(unsigned i0, unsigned i1, unsigned i2, unsigned i3) {
    return uint8_t(i0 | (i1 << 2) | (i2 << 4) | (i3 << 6));
}

// DXT5 alpha block: a0, a1, then 16 3-bit indices (48 bits little endian, texel 0 in the low bits).
void Alpha(uint8_t* b, uint8_t a0, uint8_t a1, const unsigned idx[16]) {
    b[0] = a0;
    b[1] = a1;
    uint64_t bits = 0;

    for (int i = 0; i < 16; ++i) {
        bits |= uint64_t(idx[i] & 7) << (3 * i);
    }

    for (int i = 0; i < 6; ++i) {
        b[2 + i] = uint8_t(bits >> (8 * i));
    }
}
}  // namespace

TEST(dxt1_four_colour_block) {
    uint8_t blk[8];
    const uint8_t idx[4] = {Row(0, 1, 2, 3), 0, 0, 0};

    Colour(blk, 0xF800, 0x001F, idx);  // c0 > c1: four colours
    uint32_t out[16] = {};

    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 8, 4, 4, false, out, sizeof out / sizeof out[0]));
    CHECK_EQ(out[0], 0xFFFF0000u);
    CHECK_EQ(out[1], 0xFF0000FFu);
    CHECK_EQ(out[2], 0xFFAA0055u);
    CHECK_EQ(out[3], 0xFF5500AAu);
    CHECK_EQ(out[4], 0xFFFF0000u);  // rows 1..3: index 0
}

TEST(dxt1_three_colour_block_has_transparent_black) {
    uint8_t blk[8];
    const uint8_t idx[4] = {Row(0, 1, 2, 3), 0, 0, 0};

    Colour(blk, 0x001F, 0xF800, idx);  // c0 <= c1: three colours + transparent black
    uint32_t out[16] = {};

    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 8, 4, 4, false, out, sizeof out / sizeof out[0]));
    CHECK_EQ(out[0], 0xFF0000FFu);
    CHECK_EQ(out[1], 0xFFFF0000u);
    CHECK_EQ(out[2], 0xFF7F007Fu);  // (255 + 0) / 2 = 127
    CHECK_EQ(out[3], 0x00000000u);
}

TEST(dxt5_eight_alpha_and_six_alpha_modes) {
    uint8_t blk[16];
    const uint8_t cidx[4] = {0, 0, 0, 0};
    unsigned aidx[16] = {0, 1, 2, 3, 4, 5, 6, 7};

    Alpha(blk, 255, 0, aidx);               // a0 > a1: eight alphas
    Colour(blk + 8, 0xFFFF, 0xFFFF, cidx);  // c0 == c1: DXT5 still decodes four colours (opaque white)
    uint32_t out[16] = {};

    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 16, 4, 4, true, out, sizeof out / sizeof out[0]));
    CHECK_EQ(out[0] >> 24, 255u);
    CHECK_EQ(out[1] >> 24, 0u);
    CHECK_EQ(out[2] >> 24, (6u * 255 + 3) / 7);  // 219
    CHECK_EQ(out[2] >> 24, 219u);
    CHECK_EQ(out[7] >> 24, (1u * 255 + 3) / 7);  // 36
    CHECK_EQ(out[0] & 0xFFFFFFu, 0xFFFFFFu);

    Alpha(blk, 0, 255, aidx);  // a0 <= a1: six alphas plus 0 and 255
    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 16, 4, 4, true, out, sizeof out / sizeof out[0]));
    CHECK_EQ(out[2] >> 24, (1u * 255 + 2) / 5);  // 51
    CHECK_EQ(out[6] >> 24, 0u);
    CHECK_EQ(out[7] >> 24, 255u);
}

TEST(dxt_decode_small_levels) {
    uint8_t blk[8];
    const uint8_t idx[4] = {Row(0, 1, 0, 0), 0, 0, 0};

    Colour(blk, 0xF800, 0x001F, idx);
    uint32_t two[2] = {}, one[1] = {}, five[5] = {};

    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 8, 2, 1, false, two, sizeof two / sizeof two[0]));
    CHECK_EQ(two[0], 0xFFFF0000u);
    CHECK_EQ(two[1], 0xFF0000FFu);
    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 8, 1, 1, false, one, sizeof one / sizeof one[0]));
    CHECK_EQ(one[0], 0xFFFF0000u);
    CHECK(!Rs7_DecodeDxt(blk, 7, 8, 1, 1, false, one, sizeof one / sizeof one[0]));  // short source
    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, 16, 5, 1, false, five, 5));                // two blocks, the source holds one
}

TEST(dxt_rejects_bad_sizes) {
    uint8_t blk[16] = {};
    uint32_t out[16] = {}, tall[32] = {};

    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, 8, 4, 4, false, out, 15));         // output one texel short
    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, 4, 4, 4, false, out, 16));         // pitch below one block row
    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, 16, 4, 8, true, tall, 32));        // second DXT5 block row missing
    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, SIZE_MAX, 4, 8, false, out, 16));  // (bh - 1) * pitch overflows
    CHECK(!Rs7_DecodeDxt(blk, sizeof blk, 8, 0xFFFFFFFFu, 1, false, out,
                         16));  // bw * block bytes overflows (32-bit size_t)
    // A source big enough for 65535 x 65535 (checked before anything is read), but w * h > the output.
    CHECK(!Rs7_DecodeDxt(blk, size_t(16384) * 131072, 131072, 0xFFFFu, 0xFFFFu, false, out, 16));
    CHECK(Rs7_DecodeDxt(blk, sizeof blk, 8, 4, 4, false, out, 16));
}
