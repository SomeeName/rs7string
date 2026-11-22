#include "platform/pattern.h"
#include "test.h"

TEST(pattern_parses_bytes_and_wildcards) {
    RsxrPattern p;

    CHECK(Rsxr_ParsePattern("8B ?? 0c", p));
    CHECK_EQ(p.bytes.size(), size_t(3));
    CHECK_EQ(p.bytes[0], 0x8B);
    CHECK(p.wild[1]);
    CHECK_EQ(p.bytes[2], 0x0C);
}

TEST(pattern_rejects_bad_tokens) {
    RsxrPattern p;

    CHECK(!Rsxr_ParsePattern("8B XY", p));
    CHECK(!Rsxr_ParsePattern("", p));
}

TEST(find_all_reports_every_overlapping_match) {
    const uint8_t data[] = {0xAA, 0xAA, 0xAA, 0x01};
    RsxrPattern p;

    Rsxr_ParsePattern("AA AA", p);
    const auto hits = Rsxr_FindAll(data, sizeof data, p);

    CHECK_EQ(hits.size(), size_t(2));
    CHECK_EQ(hits[0], size_t(0));
    CHECK_EQ(hits[1], size_t(1));
}

// fix/colours2: the anchored (memchr on the rarest fixed byte) search must return exactly what the
// naive scan returns, in the same ascending order, including wildcards, overlaps, edges and maxHits.
static std::vector<size_t> NaiveFind(const uint8_t* d, size_t size, const RsxrPattern& p, size_t maxHits) {
    std::vector<size_t> h;
    const size_t n = p.bytes.size();

    for (size_t i = 0; n && i + n <= size && h.size() < maxHits; ++i) {
        size_t k = 0;

        while (k < n && (p.wild[k] || d[i + k] == p.bytes[k])) {
            ++k;
        }

        if (k == n) {
            h.push_back(i);
        }
    }

    return h;
}

TEST(find_all_anchored_equals_naive_scan) {
    std::vector<uint8_t> data(20000);
    uint32_t x = 12345;

    for (auto& b : data) {
        x = x * 1103515245u + 12345u;
        b = uint8_t((x >> 16) % 7);
    }  // small alphabet: many hits

    data[0] = 9;
    data[data.size() - 1] = 9;
    size_t hist[256];

    Rsxr_ByteHistogram(data.data(), data.size(), hist);
    const char* pats[] = {"01 02", "?? 03 ?? 04", "05 ?? ?? 06 01", "09", "06 ?? ?? ?? ?? 09",
                          "09 00", "?? ??",       "00 00 00"};

    for (const char* t : pats) {
        RsxrPattern p;

        CHECK(Rsxr_ParsePattern(t, p));

        for (size_t maxHits : {size_t(1), size_t(2), size_t(100000)}) {
            const auto want = NaiveFind(data.data(), data.size(), p, maxHits);

            CHECK(Rsxr_FindAll(data.data(), data.size(), p, maxHits, hist) == want);
            CHECK(Rsxr_FindAll(data.data(), data.size(), p, maxHits) == want);
        }
    }
}

TEST(find_all_match_at_the_last_offset) {
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
    size_t hist[256];

    Rsxr_ByteHistogram(data, sizeof data, hist);
    RsxrPattern p;

    Rsxr_ParsePattern("30 40", p);
    const auto hits = Rsxr_FindAll(data, sizeof data, p, 4, hist);

    CHECK_EQ(hits.size(), size_t(1));
    CHECK_EQ(hits[0], size_t(2));
    Rsxr_ParsePattern("30 40 50", p);
    CHECK(Rsxr_FindAll(data, sizeof data, p, 4, hist).empty());
}
