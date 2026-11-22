#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Pure converter for the prototype in docs/NATIVE-FORMAT.md. No wrapper, compression or game memory.
namespace rsxrnative {
bool IsNative(const uint8_t* body, size_t size);

enum class Status { Ok, NotNative, Bad, TooSmall };

// On Ok, replaces *out with exactly size bytes (stock body and a padded extension block, or zero padding).
// On failure, leaves *out unchanged. why is optional and always terminated when whyCap is nonzero.
// Input may alias *out; no C++ exception escapes.
Status Convert(const uint8_t* body, size_t size, std::vector<uint8_t>* out, char* why, size_t whyCap);
}  // namespace rsxrnative
