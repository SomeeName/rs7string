#pragma once
#include <cstddef>

// Copies n bytes of foreign (game) memory: false instead of a fault, and false for a null source. Any SEH exception
// counts as a fault. dst is ours and must be valid.
bool Rs7_SafeCopy(void* dst, const void* src, size_t n);
