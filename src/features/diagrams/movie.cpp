// Chord-diagram movie logic: swaps the one DoInitAction tag of an owner movie for the recipe's, hash-checked
// before and after (R7DA recipes from tools/diagram_patch.py).
#include "features/diagrams/diagrams.h"
#include <windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cctype>
#include <cstring>
#pragma comment(lib, "bcrypt.lib")

namespace {
constexpr size_t kLimit = rs7diagram::kMovieLimit;

uint32_t U32(const uint8_t* p) {
    uint32_t v;

    std::memcpy(&v, p, 4);

    return v;
}

uint16_t U16(const uint8_t* p) {
    uint16_t v;

    std::memcpy(&v, p, 2);

    return v;
}

bool Hash(const uint8_t* p, size_t n, const uint8_t* expected) {
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    // fuzz/rs7string_fuzz.vcxproj only: a fuzzer cannot forge SHA-256, so the recipe's ops would never run.
    (void)p;
    (void)expected;

    return n <= kLimit;
#else
    uint8_t h[32];

    return n <= kLimit &&
           BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(p), ULONG(n), h, 32) >= 0 &&
           std::memcmp(h, expected, 32) == 0;
#endif
}

bool Span(const uint8_t* p, size_t n, uint16_t sprite, size_t* a, size_t* b) {
    if (!p || n < 13 || n > kLimit || (std::memcmp(p, "GFX", 3) && std::memcmp(p, "FWS", 3)) || U32(p + 4) != n) {
        return false;
    }

    int bits = p[8] >> 3;
    size_t pos = 8 + (5 + 4 * bits + 7) / 8 + 4;
    if (!bits || pos > n) {
        return false;
    }

    int found = 0;

    while (pos < n) {
        size_t start = pos;
        if (n - pos < 2) {
            return false;
        }

        uint16_t word = U16(p + pos);

        pos += 2;
        size_t len = word & 63;

        if (len == 63) {
            if (n - pos < 4) {
                return false;
            }

            len = U32(p + pos);
            pos += 4;
        }

        if (len > n - pos) {
            return false;
        }

        if ((word >> 6) == 59 && len >= 2 && U16(p + pos) == sprite) {
            *a = start;
            *b = pos + len;
            ++found;
        }

        if ((word >> 6) == 0) {
            return len == 0 && pos == n && found == 1;
        }

        pos += len;
    }

    return false;
}

}  // namespace

namespace rs7diagram {
int MovieTarget(const char* name) {
    if (!name) {
        return -1;
    }

    std::string s(name);

    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return c == '\\' ? '/' : char(std::tolower(c)); });
    if (s.compare(0, 5, "data/") == 0) {
        s.erase(0, 5);
    }
    const char* const names[] = {"gfxassets/libs/commonlib.gfx", "gfxassets/views/contentpanel.gfx",
                                 "gfxassets/views/ge_game.gfx"};

    for (int i = 0; i < 3; ++i) {
        if (s == names[i]) {
            return i;
        }
    }

    return -1;
}

bool SwapMovie(const uint8_t* p, size_t n, const uint8_t* r, size_t rn, uint16_t sprite, std::vector<uint8_t>* out) {
    size_t a = 0, b = 0;
    if (!out || !r || rn < 84 || rn > kLimit || std::memcmp(r, "R7DA\x01\0\0\0", 8) || !Span(p, n, sprite, &a, &b)) {
        return false;
    }

    size_t oldSize = U32(r + 8), newSize = U32(r + 12), count = U32(r + 16);
    if (oldSize != b - a || newSize > kLimit || count > rn - 84 || !Hash(p + a, b - a, r + 20)) {
        return false;
    }

    std::vector<uint8_t> tag;

    tag.reserve(newSize);
    size_t pos = 84;

    for (size_t i = 0; i < count; ++i) {
        if (pos >= rn) {
            return false;
        }

        uint8_t op = r[pos++];

        if (op == 0) {
            if (rn - pos < 8) {
                return false;
            }

            size_t at = U32(r + pos), len = U32(r + pos + 4);

            pos += 8;
            if (!len || at > oldSize || len > oldSize - at || len > newSize - tag.size()) {
                return false;
            }

            tag.insert(tag.end(), p + a + at, p + a + at + len);
        } else if (op == 1) {
            if (rn - pos < 4) {
                return false;
            }

            size_t len = U32(r + pos);

            pos += 4;
            if (!len || len > rn - pos || len > newSize - tag.size()) {
                return false;
            }

            tag.insert(tag.end(), r + pos, r + pos + len);
            pos += len;
        } else {
            return false;
        }
    }

    if (pos != rn || tag.size() != newSize || !Hash(tag.data(), tag.size(), r + 52) || n - oldSize + newSize > kLimit) {
        return false;
    }

    std::vector<uint8_t> result(p, p + a);

    result.insert(result.end(), tag.begin(), tag.end());
    result.insert(result.end(), p + b, p + n);
    uint32_t size = uint32_t(result.size());

    std::memcpy(result.data() + 4, &size, 4);
    size_t x = 0, y = 0;
    if (!Span(result.data(), result.size(), sprite, &x, &y) || x != a || y != a + tag.size()) {
        return false;
    }

    *out = std::move(result);

    return true;
}
}  // namespace rs7diagram
