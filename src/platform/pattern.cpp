#include "platform/pattern.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

bool Rs7_ParsePattern(const char* text, Rs7Pattern& out) {
    out.bytes.clear();
    out.wild.clear();
    const char* p = text;

    while (*p) {
        while (*p == ' ') {
            ++p;
        }

        if (!*p) {
            break;
        }

        if (p[0] == '?' && p[1] == '?') {
            out.bytes.push_back(0);
            out.wild.push_back(true);
        } else if (std::isxdigit(static_cast<unsigned char>(p[0])) && std::isxdigit(static_cast<unsigned char>(p[1]))) {
            const char hex[3] = {p[0], p[1], 0};

            out.bytes.push_back(static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)));
            out.wild.push_back(false);
        } else {
            return false;
        }

        p += 2;
        if (*p && *p != ' ') {
            return false;
        }
    }

    return !out.bytes.empty();
}

void Rs7_ByteHistogram(const uint8_t* data, size_t size, size_t hist[256]) {
    for (int b = 0; b < 256; ++b) {
        hist[b] = 0;
    }

    for (size_t i = 0; i < size; ++i) {
        ++hist[data[i]];
    }
}

// The naive scan compared every pattern at every .text offset: ~720 sites x 13 MB took ~19 s at
// startup, so the sites were written ~22 s after launch - after the game had already built objects
// whose layout the sites change (the ColourManager at ~12 s, fix/colours2). memchr on the pattern's
// rarest fixed byte visits only the anchor's occurrences.
std::vector<size_t> Rs7_FindAll(const uint8_t* data, size_t size, const Rs7Pattern& p, size_t maxHits,
                                const size_t* hist) {
    std::vector<size_t> hits;
    const size_t n = p.bytes.size();
    if (n == 0 || size < n || maxHits == 0) {
        return hits;
    }

    size_t j = n;  // anchor index: a fixed byte, the rarest one when a histogram is given

    for (size_t k = 0; k < n; ++k) {
        if (p.wild[k]) {
            continue;
        }

        if (j == n || (hist && hist[p.bytes[k]] < hist[p.bytes[j]])) {
            j = k;
        }
        if (!hist) {
            break;
        }
    }

    if (j == n) {  // all wildcards: every offset matches
        for (size_t i = 0; i + n <= size && hits.size() < maxHits; ++i) {
            hits.push_back(i);
        }

        return hits;
    }

    const uint8_t anchor = p.bytes[j];
    const uint8_t* const last = data + (size - n) + j;  // last valid anchor position
    const uint8_t* q = data + j;

    while (q <= last) {
        q = static_cast<const uint8_t*>(std::memchr(q, anchor, size_t(last - q) + 1));
        if (!q) {
            break;
        }

        const size_t i = size_t(q - data) - j;
        size_t k = 0;

        while (k < n && (p.wild[k] || data[i + k] == p.bytes[k])) {
            ++k;
        }

        if (k == n) {
            hits.push_back(i);
            if (hits.size() >= maxHits) {
                break;
            }
        }

        ++q;
    }

    return hits;
}
