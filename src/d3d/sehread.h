#pragma once
#include "d3d/texlist.h"
#include <windows.h>
#include <cstdint>

// Reads game memory without faulting: a __try around the copy (an access violation, e.g. a texture
// freed under us, returns false). Addresses below 64 KB are refused outright. No allocation.
struct Rs7SehReader : Rs7MemReader {
    bool Read(uintptr_t a, void* out, size_t n) const override;
};

// [*lo, *hi) = the image of `mod` as loaded (SizeOfImage from its own PE header). False if unreadable.
bool Rs7_ImageRange(HMODULE mod, uintptr_t* lo, uintptr_t* hi);
