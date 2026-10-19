#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct Rs7Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> wild;
};

// "8B ?? 0C" -> bytes/wildcards. False on an empty pattern or a bad token.
bool Rs7_ParsePattern(const char* text, Rs7Pattern& out);
// Offsets of matches in [data, data + size), overlapping, ascending, at most maxHits. hist (optional,
// 256 counts from Rs7_ByteHistogram over the same data) picks the rarest fixed byte as the memchr anchor;
// the result is identical with or without it (only the speed differs).
std::vector<size_t> Rs7_FindAll(const uint8_t* data, size_t size, const Rs7Pattern& p, size_t maxHits = 4,
                                const size_t* hist = nullptr);
// Byte counts of [data, data + size) into hist[256].
void Rs7_ByteHistogram(const uint8_t* data, size_t size, size_t hist[256]);
