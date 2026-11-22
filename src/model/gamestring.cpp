#include "model/gamestring.h"
#include "platform/safecopy.h"
#include <cstdint>

bool rsxr::ReadGameString(const void* string, char* out, size_t cap, bool truncate, size_t* length) {
    if (length) {
        *length = 0;
    }
    if (!out || !cap) {
        return false;
    }

    out[0] = 0;
    uint32_t words[6] = {};
    if (!Rsxr_SafeCopy(words, string, sizeof words)) {
        return false;
    }

    const uint32_t address = uint32_t(uintptr_t(string));
    const uint32_t data = words[5] == address + 0x10 ? address : words[0];
    if (!data || words[4] < data) {
        return false;
    }

    size_t size = words[4] - data;

    if (size >= cap) {
        if (!truncate) {
            return false;
        }

        size = cap - 1;
    }

    if (!Rsxr_SafeCopyZero(out, reinterpret_cast<const void*>(uintptr_t(data)), size)) {
        return false;
    }

    out[size] = 0;
    if (length) {
        *length = size;
    }

    return true;
}
