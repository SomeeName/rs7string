#include "core/patch.h"
#include "core/protect.h"
#include "test.h"
#include <windows.h>
#include <cstring>

namespace {
// mov eax, imm32; ret
void EmitReturn(uint8_t* at, uint32_t value) {
    at[0] = 0xB8;
    std::memcpy(at + 1, &value, 4);
    at[5] = 0xC3;
}

struct ExecPage {
    uint8_t* p;

    ExecPage() {
        p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        std::memset(p, 0xCC, 4096);
    }

    ~ExecPage() { VirtualFree(p, 0, MEM_RELEASE); }

    void Seal() {
        DWORD old;

        VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old);
    }

    int Call(size_t off) { return reinterpret_cast<int(__cdecl*)()>(p + off)(); }

    DWORD Prot() {
        MEMORY_BASIC_INFORMATION mbi{};

        VirtualQuery(p, &mbi, sizeof mbi);

        return mbi.Protect;
    }
};
}  // namespace

TEST(protect_uses_the_syscall_on_wow64) {
    CHECK(std::strcmp(Rs7_ProtectMethod(), "syscall") == 0);
}

TEST(write_jmp_redirects_and_restores_protection) {
    ExecPage pg;

    EmitReturn(pg.p + 0, 1);
    EmitReturn(pg.p + 64, 2);
    pg.Seal();
    CHECK_EQ(pg.Call(0), 1);
    CHECK(Rs7_WriteJmp(uintptr_t(pg.p), 6, uintptr_t(pg.p + 64)));
    CHECK_EQ(pg.Call(0), 2);
    CHECK_EQ(pg.p[5], uint8_t(0x90));
    CHECK_EQ(pg.Prot(), DWORD(PAGE_EXECUTE_READ));
}

TEST(atomic_jmp_inside_one_qword) {
    ExecPage pg;

    EmitReturn(pg.p + 3, 1);  // 3 + 5 = 8: the jmp fits in the qword at p
    EmitReturn(pg.p + 64, 2);
    pg.Seal();
    CHECK(Rs7_WriteJmpAtomic8(uintptr_t(pg.p + 3), uintptr_t(pg.p + 64)));
    CHECK_EQ(pg.Call(3), 2);
    CHECK(!Rs7_WriteJmpAtomic8(uintptr_t(pg.p + 4), uintptr_t(pg.p + 64)));  // would cross the qword
}

TEST(rel32_retarget_atomic_and_plain) {
    ExecPage pg;

    // p+3: call rel32 -> p+64 ; ret   (rel32 at p+4, 4-aligned)
    pg.p[3] = 0xE8;
    int32_t rel = int32_t((pg.p + 64) - (pg.p + 8));

    std::memcpy(pg.p + 4, &rel, 4);
    pg.p[8] = 0xC3;
    EmitReturn(pg.p + 64, 1);
    EmitReturn(pg.p + 128, 2);
    EmitReturn(pg.p + 192, 3);
    pg.Seal();
    CHECK_EQ(pg.Call(3), 1);
    CHECK(Rs7_WriteRel32(uintptr_t(pg.p + 4), uintptr_t(pg.p + 8), uintptr_t(pg.p + 128), true));
    CHECK_EQ(pg.Call(3), 2);
    CHECK(Rs7_WriteRel32(uintptr_t(pg.p + 4), uintptr_t(pg.p + 8), uintptr_t(pg.p + 192), false));
    CHECK_EQ(pg.Call(3), 3);
    CHECK(!Rs7_WriteRel32(uintptr_t(pg.p + 5), uintptr_t(pg.p + 9), uintptr_t(pg.p + 192), true));  // unaligned
}

TEST(write_ptr_atomic_swaps_a_readonly_slot_and_restores_protection) {
    auto* p = static_cast<uintptr_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    p[3] = 0x11223344;
    DWORD old = 0;

    VirtualProtect(p, 4096, PAGE_READONLY, &old);
    uintptr_t prev = 0;

    CHECK(Rs7_WritePtrAtomic(uintptr_t(&p[3]), 0x55667788, &prev));
    CHECK_EQ(prev, uintptr_t(0x11223344));
    CHECK_EQ(p[3], uintptr_t(0x55667788));
    MEMORY_BASIC_INFORMATION mbi{};

    VirtualQuery(p, &mbi, sizeof mbi);
    CHECK_EQ(mbi.Protect, DWORD(PAGE_READONLY));
    CHECK(!Rs7_WritePtrAtomic(uintptr_t(p) + 2, 1, &prev));  // unaligned
    VirtualFree(p, 0, MEM_RELEASE);
}
