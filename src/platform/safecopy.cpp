#include "platform/safecopy.h"
#include <windows.h>
#include <intrin.h>

namespace {
// Bad memory only: an access violation or a failed page-in. Anything else (a guard page, which the stack and some
// allocators use on purpose) goes on to its own handler.
int Filter(unsigned long code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
                                                                                 : EXCEPTION_CONTINUE_SEARCH;
}

// The one guarded copy behind every helper. REP MOVSB, not memcpy: the CRT memcpy uses XMM registers, and light
// mid-hook callbacks (which read through these helpers) run without saving them (platform/midhook.h).
bool GuardedCopy(void* dst, const void* src, size_t n) {
    __try {
        __movsb(static_cast<unsigned char*>(dst), static_cast<const unsigned char*>(src), n);
        return true;
    } __except (Filter(GetExceptionCode())) {
        return false;
    }
}
}  // namespace

bool Rsxr_SafeCopy(void* dst, const void* src, size_t n) {
    return src && GuardedCopy(dst, src, n);
}

bool Rsxr_SafeCopyZero(void* dst, const void* src, size_t n) {
    if (GuardedCopy(dst, src, n)) {
        return true;
    }

    __stosb(static_cast<unsigned char*>(dst), 0, n);

    return false;
}

bool Rsxr_SafeStore(void* dst, const void* src, size_t n) {
    return GuardedCopy(dst, src, n);
}
