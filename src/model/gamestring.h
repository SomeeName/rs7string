#pragma once
#include <cstddef>

namespace rs7 {
// The game's 32-bit string: inline data at +0 while +14h == this+10h, otherwise +0 is a pointer;
// +10h is the end. Returns false and an empty buffer on a fault or oversize value. With truncate,
// copies at most cap-1 bytes. Always terminates when out is non-null and cap > 0; length excludes it.
bool ReadGameString(const void* string, char* out, size_t cap, bool truncate = false, size_t* length = nullptr);
}  // namespace rs7
