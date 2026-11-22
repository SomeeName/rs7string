#pragma once
#include "test.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace rsxrtest {
// Small synthetic callees for game functions with register arguments.
struct Executable {
    uint8_t* p;

    explicit Executable(std::initializer_list<uint8_t> bytes) {
        p = Must(static_cast<uint8_t*>(
                     VirtualAlloc(nullptr, bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)),
                 "VirtualAlloc");
        std::memcpy(p, bytes.begin(), bytes.size());
    }

    ~Executable() { VirtualFree(p, 0, MEM_RELEASE); }

    Executable(const Executable&) = delete;
    Executable& operator=(const Executable&) = delete;
};
}  // namespace rsxrtest
