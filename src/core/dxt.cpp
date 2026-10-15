#include "core/dxt.h"

namespace {
void Expand565(uint16_t c, unsigned rgb[3]) {
    const unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;

    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

uint32_t Pack(unsigned a, const unsigned rgb[3]) {
    return (uint32_t(a) << 24) | (uint32_t(rgb[0]) << 16) | (uint32_t(rgb[1]) << 8) | uint32_t(rgb[2]);
}

// 8-byte colour block -> 16 texels (row-major). fourAlways: DXT5 ignores the c0 <= c1 mode.
void ColourBlock(const uint8_t* b, bool fourAlways, uint32_t texel[16]) {
    const uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
    unsigned p[4][3];

    Expand565(c0, p[0]);
    Expand565(c1, p[1]);
    uint32_t pal[4];

    pal[0] = Pack(255, p[0]);
    pal[1] = Pack(255, p[1]);

    if (fourAlways || c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            p[2][k] = (2 * p[0][k] + p[1][k] + 1) / 3;
            p[3][k] = (p[0][k] + 2 * p[1][k] + 1) / 3;
        }

        pal[2] = Pack(255, p[2]);
        pal[3] = Pack(255, p[3]);
    } else {
        for (int k = 0; k < 3; ++k) {
            p[2][k] = (p[0][k] + p[1][k]) / 2;
        }

        pal[2] = Pack(255, p[2]);
        pal[3] = 0;
    }

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            texel[y * 4 + x] = pal[(b[4 + y] >> (2 * x)) & 3];
        }
    }
}

// 8-byte DXT5 alpha block -> 16 alpha values.
void AlphaBlock(const uint8_t* b, unsigned alpha[16]) {
    const unsigned a0 = b[0], a1 = b[1];
    unsigned pal[8] = {a0, a1};

    if (a0 > a1) {
        for (unsigned i = 2; i < 8; ++i) {
            pal[i] = ((8 - i) * a0 + (i - 1) * a1 + 3) / 7;
        }
    } else {
        for (unsigned i = 2; i < 6; ++i) {
            pal[i] = ((6 - i) * a0 + (i - 1) * a1 + 2) / 5;
        }

        pal[6] = 0;
        pal[7] = 255;
    }

    uint64_t bits = 0;

    for (int i = 0; i < 6; ++i) {
        bits |= uint64_t(b[2 + i]) << (8 * i);
    }

    for (int i = 0; i < 16; ++i) {
        alpha[i] = pal[(bits >> (3 * i)) & 7];
    }
}
}  // namespace

bool Rs7_DecodeDxt(const uint8_t* src, size_t srcBytes, size_t srcPitch, unsigned w, unsigned h, bool dxt5,
                   uint32_t* out) {
    if (!src || !out || w == 0 || h == 0) {
        return false;
    }

    const size_t blockBytes = dxt5 ? 16 : 8;
    const size_t bw = (size_t(w) + 3) / 4, bh = (size_t(h) + 3) / 4;
    if (srcPitch < bw * blockBytes) {
        return false;
    }
    if ((bh - 1) * srcPitch + bw * blockBytes > srcBytes) {
        return false;
    }

    for (size_t by = 0; by < bh; ++by) {
        for (size_t bx = 0; bx < bw; ++bx) {
            const uint8_t* b = src + by * srcPitch + bx * blockBytes;
            uint32_t texel[16];

            ColourBlock(dxt5 ? b + 8 : b, dxt5, texel);

            if (dxt5) {
                unsigned alpha[16];

                AlphaBlock(b, alpha);

                for (int i = 0; i < 16; ++i) {
                    texel[i] = (texel[i] & 0x00FFFFFFu) | (uint32_t(alpha[i]) << 24);
                }
            }

            for (size_t y = 0; y < 4; ++y) {
                const size_t py = by * 4 + y;
                if (py >= h) {
                    break;
                }

                for (size_t x = 0; x < 4; ++x) {
                    const size_t px = bx * 4 + x;
                    if (px >= w) {
                        break;
                    }

                    out[py * w + px] = texel[y * 4 + x];
                }
            }
        }
    }

    return true;
}
