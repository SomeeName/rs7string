#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Unchecked memory access for validated objects and light callbacks. These do not handle faults;
// use safecopy.h for foreign memory that may be unavailable. A field reference requires T alignment.
namespace rsxrmem {
template <class T>
T& UncheckedField(void* base, size_t off) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

template <class T>
T UncheckedRead(const void* base, size_t off = 0) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value;

    std::memcpy(&value, static_cast<const uint8_t*>(base) + off, sizeof value);

    return value;
}

template <class T>
T UncheckedRead(uintptr_t address) {
    return UncheckedRead<T>(reinterpret_cast<const void*>(address));
}
}  // namespace rsxrmem
