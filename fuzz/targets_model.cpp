// Fuzz targets: the RS7X extension block (model/extblock.h) and the DXT1/DXT5 decoder (platform/dxt.h).
#include "fuzz.h"
#include "model/extblock.h"
#include "platform/dxt.h"
#include <cstring>

namespace rs7fuzz {
namespace {
// docs/release/RS7X-SPEC.md "Example" (tests/model/test_extblock.cpp): 7 strings, two templates, one ChordNotes record.
const uint8_t kExample[] = {
    'R', 'S', '7', 'X', 1, 0, 16, 0, 0x4E, 0,    0,    0,    3,    0,    0,    0,   'S', 'T', 'R', 'S',
    1,   0,   1,   0,   5, 0, 0,  0, 0,    7,    1,    0,    6,    'C',  'H',  'D', 'T', 1,   0,   1,
    0,   8,   0,   0,   0, 2, 0,  0, 0,    2,    1,    0xFF, 0xFF, 'C',  'H',  'N', 'T', 1,   0,   0,
    0,   13,  0,   0,   0, 1, 0,  0, 0,    0x00, 0x20, 0,    0,    0xFF, 0xFF, 0,   0,   0,
};
}  // namespace

// [templates % 16][chordNotes % 16][tuning % 10][block bytes...]
int Rs7x(const uint8_t* d, size_t n) {
    if (n < 3) {
        return 0;
    }

    const uint32_t templates = d[0] % 16, chordNotes = d[1] % 16, tuning = d[2] % 10;
    rs7ext::Block b;
    char why[160];

    if (rs7ext::Parse(d + 3, n - 3, templates, chordNotes, tuning, &b, why, sizeof why) != rs7ext::Status::Ok) {
        return 0;
    }

    // An accepted block must hold together for its readers (features/nslot, features/diagrams).
    for (uint32_t t = 0; t <= templates; ++t) {
        for (int s = -1; s <= rs7ext::kMaxStrings; ++s) {
            int fret = 0, finger = 0;

            b.TemplateColumn(t, s, &fret, &finger);
            b.Column(s);
        }
    }

    volatile size_t sink = 0;

    for (const rs7ext::Tech& t : b.tech) {
        for (const rs7ext::Bend& e : t.bends) {
            sink = sink + size_t(e.reserved);
        }
    }

    return 0;
}

Seeds Rs7xSeeds() {
    std::vector<uint8_t> s = {2, 1, 7};

    s.insert(s.end(), kExample, kExample + sizeof kExample);
    Seeds out = {s};

    out.push_back(std::vector<uint8_t>(s.begin(), s.begin() + 3 + 16));  // header only
    out.push_back({0, 0, 7, 'R', 'S', '7', 'X'});

    return out;
}

// [w][h][flags: 1 dxt5, 2 huge sizes][pitch delta][short output][blocks...]
int Dxt(const uint8_t* d, size_t n) {
    if (n < 5) {
        return 0;
    }

    const bool dxt5 = d[2] & 1, huge = (d[2] & 2) != 0;
    const unsigned w = huge ? 0x40000000u + d[0] : d[0] % 33 + 1u, h = huge ? 0x10000u + d[1] : d[1] % 33 + 1u;
    const size_t nominal = size_t((w + 3) / 4) * (dxt5 ? 16 : 8);
    const size_t pitch = nominal + size_t(int8_t(d[3]));
    const size_t pixels = huge ? 64 : size_t(w) * h;
    const size_t shortBy = d[4] % 4u;
    std::vector<uint32_t> out(pixels > shortBy ? pixels - shortBy : 0);
    // An exact-size copy: a read past the source is a heap overflow for ASan.
    std::vector<uint8_t> src(d + 5, d + n);

    Rs7_DecodeDxt(src.empty() ? nullptr : src.data(), src.size(), pitch, w, h, dxt5, out.empty() ? nullptr : out.data(),
                  out.size());

    return 0;
}

Seeds DxtSeeds() {
    std::vector<uint8_t> dxt1 = {7, 7, 0, 0, 0}, dxt5 = {3, 3, 1, 0, 0};

    dxt1.resize(5 + 2 * 2 * 8, 0x5A);
    dxt5.resize(5 + 16, 0xA5);

    return {dxt1, dxt5, {1, 1, 2, 0, 0, 0, 0}};
}
}  // namespace rs7fuzz
