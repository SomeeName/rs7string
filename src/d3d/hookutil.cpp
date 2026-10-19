#include "d3d/hookutil.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace rs7d3d {
void LogQuiet(const char* fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    try {
        Rs7Log("%s", buf);
    } catch (...) {
    }
}

void ModuleName(const void* a, char* out, size_t n) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = {};

    if (!a ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(a), &m) ||
        !GetModuleFileNameA(m, path, MAX_PATH)) {
        std::snprintf(out, n, "no module");
        return;
    }

    const char* base = path;

    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    std::snprintf(out, n, "%s", base);
}
}  // namespace rs7d3d
