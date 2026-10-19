#include "platform/safecopy.h"
#include <windows.h>
#include <cstring>

bool Rs7_SafeCopy(void* dst, const void* src, size_t n) {
    if (!src) {
        return false;
    }

    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
