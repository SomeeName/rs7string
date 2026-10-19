#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "test.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <map>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace {
using Properties = std::map<std::string, int>;

void PutProperty(void* p, const char* key, int value) {
    (*static_cast<Properties*>(p))[key] = value;
}

void Word(std::vector<uint8_t>& b, size_t p, uint32_t n) {
    std::memcpy(b.data() + p, &n, 4);
}

// Entirely synthetic uncompressed movie: RECT, frame header, DoInitAction, End.
std::vector<uint8_t> Movie() {
    std::vector<uint8_t> b = {'F', 'W', 'S', 8, 0, 0, 0, 0, 8, 0, 0, 12, 1, 0, 0xc3, 0x0e, 28, 0, 0, 0, 0};

    Word(b, 4, uint32_t(b.size()));

    return b;
}

std::vector<uint8_t> Recipe(const std::vector<uint8_t>& movie) {
    // One COPY of the synthetic five-byte DoInitAction tag.
    std::vector<uint8_t> r(93, 0);

    std::memcpy(r.data(), "R7DA\1\0\0\0", 8);
    Word(r, 8, 5);
    Word(r, 12, 5);
    Word(r, 16, 1);
    Word(r, 85, 0);
    Word(r, 89, 5);
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(movie.data() + 14), 5, r.data() + 20,
                     32) >= 0);
    std::memcpy(r.data() + 52, r.data() + 20, 32);

    return r;
}

void Refuses(const std::vector<uint8_t>& m, const std::vector<uint8_t>& r) {
    std::vector<uint8_t> out = {0xab, 0xcd};

    CHECK(!rs7diagram::SwapMovie(m.data(), m.size(), r.data(), r.size(), 28, &out));
    CHECK(out == std::vector<uint8_t>({0xab, 0xcd}));
}
}  // namespace

TEST(diagram_manifest_open_capo_has_no_fretted_window) {
    for (int count : {7, 8}) {
        for (int capo = 0; capo <= 24; ++capo) {
            rs7diagram::Shape s;

            s.manifest = true;
            s.count = count;
            s.capo = capo;
            s.frets[0] = s.frets[count - 1] = capo;
            Properties out;

            rs7diagram::Emit(s, PutProperty, &out);
            CHECK_EQ(out["StartFret"], 1);
            CHECK_EQ(out["LowEFret"], 0);
            CHECK_EQ(out[count == 7 ? "LowBFret" : "LowFsFret"], 0);
        }
    }
}

TEST(diagram_manifest_high_frets_use_actual_minimum) {
    for (int fret = 25; fret <= 30; ++fret) {
        rs7diagram::Shape s;

        s.manifest = true;
        s.count = 7;
        s.frets[6] = fret;
        Properties out;

        rs7diagram::Emit(s, PutProperty, &out);
        CHECK_EQ(out["StartFret"], fret);
        CHECK_EQ(out["LowBFret"], 1);
    }
}

TEST(diagram_t8_corrupt_widths_refuse_without_mutating_shape) {
    uint8_t t[rs7ns::kT8Size] = {};

    for (int count = 0; count < 256; ++count) {
        t[rs7ns::kT8Slots] = uint8_t(count);
        rs7diagram::Shape s;

        s.count = 4;
        s.frets[0] = 17;
        s.capo = 3;
        s.manifest = true;
        const bool ok = rs7diagram::FromT8(t, &s);

        CHECK_EQ(ok, count == 7 || count == 8);

        if (!ok) {
            CHECK_EQ(s.count, 4);
            CHECK_EQ(s.frets[0], 17);
            CHECK_EQ(s.capo, 3);
            CHECK(s.manifest);
        }
    }

    rs7diagram::Shape s;

    CHECK(!rs7diagram::FromT8(nullptr, &s));
    CHECK(!rs7diagram::FromT8(t, nullptr));

    for (int count : {-1, 0, 4, 6, 9, 255}) {
        s.count = count;
        Properties out;

        rs7diagram::Emit(s, PutProperty, &out);
        CHECK(out.empty());
    }
}

TEST(diagrammovie_synthetic_recipe_roundtrip_and_every_truncated_prefix) {
    auto m = Movie(), r = Recipe(m);
    std::vector<uint8_t> out;

    CHECK(rs7diagram::SwapMovie(m.data(), m.size(), r.data(), r.size(), 28, &out));
    CHECK(out == m);

    for (size_t n = 0; n < r.size(); ++n) {
        Refuses(m, {r.begin(), r.begin() + n});
    }

    for (size_t n = 0; n < m.size(); ++n) {
        std::vector<uint8_t> truncated(m.begin(), m.begin() + n);

        if (n >= 8) {
            Word(truncated, 4, uint32_t(n));
        }
        Refuses(truncated, r);
    }

    CHECK(!rs7diagram::SwapMovie(nullptr, m.size(), r.data(), r.size(), 28, &out));
    CHECK(!rs7diagram::SwapMovie(m.data(), m.size(), nullptr, r.size(), 28, &out));
    CHECK(!rs7diagram::SwapMovie(m.data(), m.size(), r.data(), r.size(), 28, nullptr));
}

TEST(diagrammovie_recipe_corruption_bounds_and_output_atomicity) {
    const auto m = Movie(), good = Recipe(m);

    for (size_t pos = 0; pos < good.size(); ++pos) {
        for (int bit = 0; bit < 8; ++bit) {
            auto r = good;

            r[pos] ^= uint8_t(1 << bit);
            Refuses(m, r);
        }
    }

    for (size_t pos : {size_t(8), size_t(12), size_t(16), size_t(85), size_t(89)}) {
        auto r = good;

        Word(r, pos, 0xffffffffu);
        Refuses(m, r);
    }

    auto r = good;

    r.push_back(0);
    Refuses(m, r);
    auto duplicate = m;

    duplicate.insert(duplicate.end() - 2, m.begin() + 14, m.begin() + 19);
    Word(duplicate, 4, uint32_t(duplicate.size()));
    Refuses(duplicate, good);
    auto longHeader = m;

    longHeader[14] = 0xff;
    longHeader[15] = 0x0e;
    Word(longHeader, 16, 0xffffffffu);
    Refuses(longHeader, good);
    auto missingEnd = m;

    missingEnd.resize(missingEnd.size() - 2);
    Word(missingEnd, 4, uint32_t(missingEnd.size()));
    Refuses(missingEnd, good);
}
