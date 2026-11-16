#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Copies n bytes of foreign (game) memory: false instead of a fault, and false for a null source. An access violation
// or a failed page-in counts as a fault; a guard page goes on to its own handler (safecopy.cpp). dst is ours and must
// be valid.
bool Rs7_SafeCopy(void* dst, const void* src, size_t n);
// Like Rs7_SafeCopy, but zero-fills dst on a fault (and for a null src, which faults). dst is ours and must be valid.
bool Rs7_SafeCopyZero(void* dst, const void* src, size_t n);
// Writes n bytes into foreign (game) memory: false instead of a fault (the same faults as Rs7_SafeCopy).
bool Rs7_SafeStore(void* dst, const void* src, size_t n);

// Reads a T at base + off in foreign (game) memory; a zero T on a fault (including a null base).
template <class T>
T Rs7_SafeRead(const void* base, size_t off) {
    static_assert(std::is_trivially_copyable_v<T>, "Rs7_SafeRead copies raw bytes");
    T v{};

    if (!base) {
        return v;
    }

    Rs7_SafeCopyZero(&v, reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(base) + off), sizeof v);

    return v;
}
