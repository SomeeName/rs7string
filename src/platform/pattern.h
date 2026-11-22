#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct RsxrPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> wild;
};

// "8B ?? 0C" -> bytes/wildcards. False on an empty pattern or a bad token.
bool Rsxr_ParsePattern(const char* text, RsxrPattern& out);
// Offsets of matches in [data, data + size), overlapping, ascending, at most maxHits. hist (optional,
// 256 counts from Rsxr_ByteHistogram over the same data) picks the rarest fixed byte as the memchr anchor;
// the result is identical with or without it (only the speed differs).
std::vector<size_t> Rsxr_FindAll(const uint8_t* data, size_t size, const RsxrPattern& p, size_t maxHits = 4,
                                 const size_t* hist = nullptr);
// Byte counts of [data, data + size) into hist[256].
void Rsxr_ByteHistogram(const uint8_t* data, size_t size, size_t hist[256]);
