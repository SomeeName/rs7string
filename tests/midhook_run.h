#pragma once
// The register harness of the native mid-hook call tests (test_midhook_call.cpp, test_midhook_wrap.cpp): Run calls a
// function with given registers and flags (EBP and ESP stay the C frame's) and returns the registers, flags and ESP
// delta after it. Machine code, no inline assembly.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace Rs7MidRun {
struct Regs {
    uint32_t eax, ebx, ecx, edx, esi, edi, flags, espDelta;
};

inline Regs g_in, g_out;
inline uintptr_t g_fn;
inline uint32_t g_esp;
inline constexpr uint32_t kFlagPatterns[] = {0x202, 0x2d7, 0xed7};  // IF; CF|PF|AF|ZF|SF; all arithmetic + DF + OF

inline void Dword(uint8_t* p, uint32_t v) {
    std::memcpy(p, &v, 4);
}

// Executable helpers as machine code (no inline assembly in this file): Run's register harness, which calls g_fn with
// g_in's registers and flags (EBP and ESP stay the C frame's) and stores the result in g_out, and an x87/SSE clobber.
struct Code {
    uint8_t* p = nullptr;
    size_t n = 0;

    Code() {
        p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!p) {
            return;
        }

        Bytes({0x60, 0x9c, 0x89, 0x25}, &g_esp);  // pushad; pushfd; mov [g_esp],esp
        const uint8_t loads[] = {0x1d, 0x0d, 0x15, 0x35, 0x3d};
        const uint32_t* in[] = {&g_in.ebx, &g_in.ecx, &g_in.edx, &g_in.esi, &g_in.edi};

        Bytes({0xa1}, &g_in.eax);

        for (int r = 0; r < 5; ++r) {
            Bytes({0x8b, loads[r]}, in[r]);
        }

        Bytes({0xff, 0x35}, &g_in.flags);  // push [flags]
        Bytes({0x9d, 0xff, 0x15}, &g_fn);  // popfd; call [g_fn]
        Bytes({0x9c, 0x8f, 0x05}, &g_out.flags);
        Bytes({0xa3}, &g_out.eax);
        const uint32_t* out[] = {&g_out.ebx, &g_out.ecx, &g_out.edx, &g_out.esi, &g_out.edi};

        for (int r = 0; r < 5; ++r) {
            Bytes({0x89, loads[r]}, out[r]);
        }

        Bytes({0x8b, 0xc4, 0x2b, 0x05}, &g_esp);  // mov eax,esp; sub eax,[g_esp]
        Bytes({0xa3}, &g_out.espDelta);
        Bytes({0x8b, 0x25}, &g_esp);
        Bytes({0x9d, 0x61, 0xc3}, nullptr);  // popfd; popad; ret
        clobber = p + n;
        Bytes({0xdb, 0xe3, 0x0f, 0x57, 0xc0, 0xc3}, nullptr);  // fninit; xorps xmm0,xmm0; ret
    }

    ~Code() {
        if (p) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }

    void Bytes(std::initializer_list<uint8_t> op, const void* address) {
        for (const uint8_t b : op) {
            p[n++] = b;
        }

        if (address) {
            Dword(p + n, uint32_t(uintptr_t(address)));
            n += 4;
        }
    }

    uint8_t* clobber = nullptr;
};

inline Code& Helpers() {
    static Code code;

    return code;
}

inline Regs Run(uintptr_t fn, uint32_t flags, uint32_t ecx = 5) {
    g_in = {0x1111, 0x2222, ecx, 0x4444, 0x5555, 0x6666, flags, 0};
    g_fn = fn;
    reinterpret_cast<void (*)()>(Helpers().p)();

    return g_out;
}

inline bool Same(const Regs& a, const Regs& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

}  // namespace Rs7MidRun
