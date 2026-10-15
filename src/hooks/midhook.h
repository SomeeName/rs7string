#pragma once
#include <cstddef>
#include <cstdint>

namespace Rs7Mid {
// PUSHAD order. ESP is the site's original ESP (not PUSHAD's saved ESP).
// jump defaults to the relocated replay; assigning another address skips replay.
// callback is thunk-private; callbacks must leave it alone and must not throw.
struct Context {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, eflags;
    uint32_t callback, jump;

    // Emulate x86 CMP lhs,rhs (operand width 8, 16 or 32 bits) without losing non-arithmetic flags
    // (including DF). Useful when replacing a stub whose own comparisons remain observable.
    void Compare(uint32_t lhs, uint32_t rhs, unsigned bits) {
        const uint32_t sign = 1u << (bits - 1);
        const uint32_t mask = bits == 32 ? 0xffffffffu : (1u << bits) - 1;

        lhs &= mask;
        rhs &= mask;
        const uint32_t result = (lhs - rhs) & mask;
        uint32_t parity = result & 0xff;

        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;
        const uint32_t arithmetic = (lhs < rhs ? 1u : 0u) | (!(parity & 1) ? 4u : 0u) | ((lhs ^ rhs ^ result) & 0x10) |
                                    (result == 0 ? 0x40u : 0u) | (result & sign ? 0x80u : 0u) |
                                    (((lhs ^ rhs) & (lhs ^ result)) & sign ? 0x800u : 0u);

        eflags = (eflags & ~0x8d5u) | arithmetic;
    }

    void Compare32(uint32_t lhs, uint32_t rhs) { Compare(lhs, rhs, 32); }
};

using Callback = void(__cdecl*)(Context&);
static_assert(sizeof(Context) == 44 && offsetof(Context, jump) == 40, "thunk ABI");
static_assert(offsetof(Context, esp) == 12 && offsetof(Context, eflags) == 32 && offsetof(Context, callback) == 36,
              "thunk register/flags ABI");

// Owns executable storage, not a live patch. Prepare before writing any site.
// Destroy only after restoring the site AND quiescing every thread in the hook.
class Hook {
public:
    Hook() = default;
    ~Hook();
    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    bool Prepare(uintptr_t site, const uint8_t* bytes, size_t length, Callback callback);

    uintptr_t Entry() const { return reinterpret_cast<uintptr_t>(code_); }

private:
    void* code_ = nullptr;
};
}  // namespace Rs7Mid

// Pure bounded emitters, also executed directly by the emulator tests. Return
// bytes emitted or 0 on rejection. Source span must be whole instructions, 5..16
// bytes. Relocate output may be partially written on failure; never execute it.
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_Relocate(uint8_t* out, size_t capacity, const uint8_t* source,
                                                                uintptr_t sourceVa, size_t length, uintptr_t destVa);
extern "C" __declspec(dllexport) size_t __cdecl Rs7Mid_Build(uint8_t* out, size_t capacity, const uint8_t* source,
                                                             uintptr_t sourceVa, size_t length, uintptr_t destVa,
                                                             uintptr_t callback);
extern "C" __declspec(dllexport) void Rs7Mid_Thunk();
