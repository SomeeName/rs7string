#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Copies n bytes of foreign (game) memory: false instead of a fault, and false for a null source. An access violation
// or a failed page-in counts as a fault; a guard page goes on to its own handler (safecopy.cpp). dst is ours and must
// be valid.
bool Rsxr_SafeCopy(void* dst, const void* src, size_t n);
// Like Rsxr_SafeCopy, but zero-fills dst on a fault (and for a null src, which faults). dst is ours and must be valid.
bool Rsxr_SafeCopyZero(void* dst, const void* src, size_t n);
// Writes n bytes into foreign (game) memory: false instead of a fault (the same faults as Rsxr_SafeCopy).
bool Rsxr_SafeStore(void* dst, const void* src, size_t n);

// Reads a T at base + off in foreign (game) memory; a zero T on a fault (including a null base).
template <class T>
T Rsxr_SafeRead(const void* base, size_t off) {
    static_assert(std::is_trivially_copyable_v<T>, "Rsxr_SafeRead copies raw bytes");
    T v{};

    if (!base) {
        return v;
    }

    Rsxr_SafeCopyZero(&v, reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(base) + off), sizeof v);

    return v;
}
