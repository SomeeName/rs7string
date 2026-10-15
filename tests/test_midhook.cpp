#include "hooks/midhook.h"
#include "hooks/registry.h"
#include "test.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

TEST(midhook_relocation_and_rejections) {
    uint8_t out[128]{};
    const uint8_t branch[] = {0x75, 2, 0x90, 0x90, 0x90};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, branch, 0x1000, 5, 0x2000), 9u);
    CHECK_EQ(out[0], 0x0f);
    CHECK_EQ(out[1], 0x85);
    int32_t rel;

    std::memcpy(&rel, out + 2, 4);
    CHECK_EQ(0x2006 + rel, 0x2008);  // internal target remapped after widened branch
    const uint8_t call[] = {0xe8, 0xfb, 0x0f, 0, 0};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, call, 0x1000, 5, 0x3000), 5u);
    std::memcpy(&rel, out + 1, 4);
    CHECK_EQ(0x3005 + rel, 0x2000);
    const uint8_t middle[] = {0xeb, 1, 0x8b, 0xc0, 0x90};
    const uint8_t loop[] = {0xe2, 0xfe, 0x90, 0x90, 0x90};
    const uint8_t pic[] = {0xe8, 0, 0, 0, 0};
    const uint8_t cut[] = {0x8b, 0x84, 0x24, 0, 0};
    const uint8_t prefix[] = {0x66, 0x90, 0x90, 0x90, 0x90};

    for (const auto* p : {middle, loop, pic, cut, prefix}) {
        CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, p, 0x1000, 5, 0x2000), 0u);
    }

    CHECK_EQ(Rs7Mid_Relocate(out, 5, branch, 0x1000, 5, 0x2000), 0u);
}

TEST(midhook_rejects_internal_call_pop_eip_materialization) {
    uint8_t out[128]{};
    const uint8_t pic[] = {0xe8, 1, 0, 0, 0, 0x90, 0x58};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, pic, 0x1000, sizeof pic, 0x2000), 0u);
}

namespace {
void __cdecl Noop(Rs7Mid::Context&) {}

void __cdecl ClobberFp(Rs7Mid::Context&) {
    __asm {
        fninit
        xorps xmm0, xmm0
        xorps xmm7, xmm7
    }
}
}  // namespace

TEST(midhook_native_build_and_execute) {
    // mov eax,7; ret: only MOV is stolen; the original RET is the continuation.
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    const uint8_t original[] = {0xb8, 7, 0, 0, 0, 0xc3};

    std::memcpy(page, original, sizeof original);
    CHECK(Rs7Mid_Build(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), uintptr_t(&Noop)) > 0);
    CHECK_EQ(reinterpret_cast<int (*)()>(page + 64)(), 7);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST(midhook_prepare_rejects_unsafe_span_without_an_entry) {
    Rs7Mid::Hook hook;
    const uint8_t bad[] = {0xe2, 0xfe, 0x90, 0x90, 0x90};

    CHECK(!hook.Prepare(0x1000, bad, sizeof bad, &Noop));
    CHECK_EQ(hook.Entry(), 0u);
    const uint8_t good[] = {0xb8, 7, 0, 0, 0};

    CHECK(hook.Prepare(0x1000, good, sizeof good, &Noop));
    CHECK(hook.Entry() != 0);
    CHECK(!hook.Prepare(0x1000, good, sizeof good, &Noop));
}

TEST(midhook_native_preserves_x87_sse_state) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    const uint8_t raw[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};

    std::memcpy(page, raw, sizeof raw);
    CHECK(Rs7Mid_Build(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), uintptr_t(&ClobberFp)) > 0);
    __declspec(align(16)) uint8_t original[512]{}, before[512]{}, after[512]{};
    auto fn = reinterpret_cast<void (*)()>(page + 64);

    __asm {
        fxsave original
        fninit
        fld1
        fldpi
        pcmpeqd xmm0, xmm0
        pcmpeqd xmm7, xmm7
        fxsave before
        call fn
        fxsave after
        fxrstor original
    }

    // Reserved bytes / selectors are implementation-dependent. Compare the
    // documented control/status/tag, pointer, MXCSR, ST and XMM fields.
    CHECK(std::memcmp(before, after, 5) == 0);
    CHECK(std::memcmp(before + 6, after + 6, 22) == 0);
    CHECK(std::memcmp(before + 32, after + 32, 256) == 0);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST(midhook_native_overhead_per_call) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    const uint8_t raw[] = {0xb8, 7, 0, 0, 0, 0xc3};

    std::memcpy(page, raw, sizeof raw);
    CHECK(Rs7Mid_Build(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), uintptr_t(&Noop)) > 0);
    LARGE_INTEGER freq, start, end;

    QueryPerformanceFrequency(&freq);
    double measurements[2][7]{};
    constexpr int calls = 300000;
    volatile int sink = 0;

    for (int sample = 0; sample < 7; ++sample) {
        for (int mode = 0; mode < 2; ++mode) {
            auto fn = reinterpret_cast<int (*)()>(page + (mode ? 64 : 0));

            for (int i = 0; i < 1000; ++i) {
                sink = fn();
            }

            QueryPerformanceCounter(&start);

            for (int i = 0; i < calls; ++i) {
                sink = fn();
            }

            QueryPerformanceCounter(&end);
            measurements[mode][sample] = double(end.QuadPart - start.QuadPart) * 1e9 / double(freq.QuadPart) / calls;
        }
    }

    for (auto& row : measurements) {
        std::sort(row, row + 7);
    }

    CHECK_EQ(sink, 7);
    std::printf("mid-hook QPC median (7 x %d): stock %.1f ns, noop %.1f ns, overhead %.1f ns/call\n", calls,
                measurements[0][3], measurements[1][3], measurements[1][3] - measurements[0][3]);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST(midhook_x87_memory_forms_and_trailing_ret) {
    uint8_t out[128]{};
    // CMUV 00518BC0: FST dword ptr [ecx+eax*4+890h] (D9 /2, mod 2, SIB) is position independent.
    const uint8_t fst[] = {0xd9, 0x94, 0x81, 0x90, 0x08, 0x00, 0x00};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, fst, 0x518bc0, sizeof fst, 0x2000), sizeof fst);
    CHECK(std::memcmp(out, fst, sizeof fst) == 0);

    // FLD / FSTP / FLDCW / FNSTCW memory forms are accepted too.
    for (uint8_t reg : {0, 2, 3, 5, 7}) {
        const uint8_t m[] = {0xd9, uint8_t(0x40 | (reg << 3)), 0x10, 0x90, 0x90};  // [eax+10h]

        CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, m, 0x1000, sizeof m, 0x2000), sizeof m);
    }

    // Register forms (FXCH/FLD1/FCHS...), FLDENV/FNSTENV and the reserved /1 stay rejected.
    const uint8_t reg[] = {0xd9, 0xc9, 0x90, 0x90, 0x90}, fld1[] = {0xd9, 0xe8, 0x90, 0x90, 0x90};
    const uint8_t env[] = {0xd9, 0x60, 0x10, 0x90, 0x90}, nst[] = {0xd9, 0x70, 0x10, 0x90, 0x90};
    const uint8_t r1[] = {0xd9, 0x48, 0x10, 0x90, 0x90};

    for (const auto* p : {reg, fld1, env, nst, r1}) {
        CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, p, 0x1000, 5, 0x2000), 0u);
    }

    // CMBUILD 0051966C: POP ESI / POP EBX / MOV ESP,EBP / POP EBP / RET - the RET is copied as is.
    const uint8_t build[] = {0x5e, 0x5b, 0x8b, 0xe5, 0x5d, 0xc3};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, build, 0x51966d, sizeof build, 0x2000), sizeof build);
    CHECK(std::memcmp(out, build, sizeof build) == 0);
    // A RET anywhere but last would leave dead stolen bytes behind it: rejected.
    const uint8_t early[] = {0x5e, 0xc3, 0x90, 0x90, 0x90, 0x90};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, early, 0x1000, sizeof early, 0x2000), 0u);
    // C2 (RET imm16) is not whitelisted.
    const uint8_t retn[] = {0x5e, 0x5b, 0x5d, 0xc2, 0x04, 0x00};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, retn, 0x1000, sizeof retn, 0x2000), 0u);
}

namespace {
int g_midCallbacks = 0;

void __cdecl Count(Rs7Mid::Context&) {
    ++g_midCallbacks;
}
}  // namespace

TEST(midhook_stolen_ret_does_not_jump_back) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    // mov eax,7 / ret stolen as a whole; the byte after the RET is INT3 (the real game has CC padding).
    const uint8_t original[] = {0xb8, 7, 0, 0, 0, 0xc3, 0xcc};

    std::memcpy(page, original, sizeof original);
    const size_t n = Rs7Mid_Build(page + 64, 256, page, uintptr_t(page), 6, uintptr_t(page + 64), uintptr_t(&Count));

    CHECK_EQ(n, 15u + 6u);  // entry stub + replay, no trailing jump back
    CHECK_EQ(page[64 + n - 1], 0xc3);
    g_midCallbacks = 0;
    CHECK_EQ(reinterpret_cast<int (*)()>(page + 64)(), 7);  // callback, replayed RET returns to the caller
    CHECK_EQ(g_midCallbacks, 1);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST(midhook_compare32_matches_hardware_cmp) {
    const uint32_t inputs[] = {0, 1, 2, 0xf, 0x10, 0x7fffffff, 0x80000000, 0xffffffff};

    for (uint32_t lhsValue : inputs) {
        for (uint32_t rhsValue : inputs) {
            uint32_t hardwareFlags;

            __asm {
            mov eax, lhsValue
            cmp eax, rhsValue
            pushfd
            pop hardwareFlags
            }

            Rs7Mid::Context context{};

            context.eflags = 0xed7;
            context.Compare32(lhsValue, rhsValue);
            CHECK_EQ(context.eflags & 0x8d5, hardwareFlags & 0x8d5);
            CHECK_EQ(context.eflags & ~0x8d5u, 0xed7u & ~0x8d5u);
        }
    }
}

TEST(midhook_compare16_and_compare8_match_hardware_cmp) {
    const uint32_t inputs[] = {0, 1, 2, 0xf, 0x10, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x12345678};

    for (uint32_t lhsValue : inputs) {
        for (uint32_t rhsValue : inputs) {
            uint32_t flags16, flags8;

            __asm {
            mov eax, lhsValue
            mov ecx, rhsValue
            cmp ax, cx
            pushfd
            pop flags16
            cmp al, cl
            pushfd
            pop flags8
            }

            Rs7Mid::Context context{};

            context.eflags = 0xed7;
            context.Compare(lhsValue, rhsValue, 16);
            CHECK_EQ(context.eflags & 0x8d5, flags16 & 0x8d5);
            CHECK_EQ(context.eflags & ~0x8d5u, 0xed7u & ~0x8d5u);
            context.eflags = 0x202;
            context.Compare(lhsValue, rhsValue, 8);
            CHECK_EQ(context.eflags & 0x8d5, flags8 & 0x8d5);
            CHECK_EQ(context.eflags & ~0x8d5u, 0x202u & ~0x8d5u);
        }
    }
}

TEST(midhook_pilot_registration) {
    for (const char* id : {"SE1", "SE2", "SE3"}) {
        const auto* hook = Rs7_FindHook(id);

        CHECK(hook && hook->callback && !hook->stub && !hook->resume);
        CHECK(hook && !hook->diagOnly && !hook->vgOnly);
    }
}
