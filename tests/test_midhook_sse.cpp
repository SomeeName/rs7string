// The sse mid-hook mode (Rs7Mid::Mode::Sse, ARCHITECTURE.md section 15 "Sse mode") on the hardware, the exact x87
// compare and ADD flag helpers W / R2 / R6 use, and the relocator's FCOMP m32. Hardware references are emitted
// machine code (Asm), no inline assembly.
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <xmmintrin.h>

namespace {
// An executable page of emitted helpers.
struct Asm {
    uint8_t* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    size_t n = 0;

    Asm() = default;
    Asm(const Asm&) = delete;
    Asm& operator=(const Asm&) = delete;

    ~Asm() {
        if (p) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }

    // Appends code (and the address of `at` as a trailing disp32 / imm32); returns where it starts.
    uint8_t* Put(std::initializer_list<uint8_t> code, const void* at = nullptr) {
        uint8_t* start = p + n;

        for (const uint8_t b : code) {
            p[n++] = b;
        }

        if (at) {
            const uint32_t address = uint32_t(uintptr_t(at));

            std::memcpy(p + n, &address, 4);
            n += 4;
        }

        return start;
    }
};

Asm g_asm;
// pcmpeqd xmm0..xmm7 (all ones); ret.
const auto g_clobberXmm = reinterpret_cast<void (*)()>(
    g_asm.Put({0x66, 0x0f, 0x76, 0xc0, 0x66, 0x0f, 0x76, 0xc9, 0x66, 0x0f, 0x76, 0xd2, 0x66, 0x0f, 0x76, 0xdb, 0x66,
               0x0f, 0x76, 0xe4, 0x66, 0x0f, 0x76, 0xed, 0x66, 0x0f, 0x76, 0xf6, 0x66, 0x0f, 0x76, 0xff, 0xc3}));
uint32_t g_addFlags = 0;
// mov eax,[esp+4]; add eax,[esp+8]; pushfd; pop dword [g_addFlags]; ret
uint8_t* const g_addCode = g_asm.Put({0x8b, 0x44, 0x24, 0x04, 0x03, 0x44, 0x24, 0x08, 0x9c, 0x8f, 0x05}, &g_addFlags);
uint8_t* const g_addRet = g_asm.Put({0xc3});
const auto g_hardwareAdd = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(g_addCode);
// mov eax,[esp+4]; fld tbyte [eax]; fcomp dword [esp+8]; fnstsw ax; fnclex; movzx eax,ax; ret
const auto g_fcomp = reinterpret_cast<uint32_t(__cdecl*)(const uint8_t*, float)>(g_asm.Put(
    {0x8b, 0x44, 0x24, 0x04, 0xdb, 0x28, 0xd8, 0x5c, 0x24, 0x08, 0xdf, 0xe0, 0xdb, 0xe2, 0x0f, 0xb7, 0xc0, 0xc3}));

float g_a = 0, g_b = 0;
bool g_loaded = false, g_otherEdits = true;
unsigned g_depth = 99;

// SSE math, MXCSR and every XMM register clobbered; the x87 result through LoadSt0.
void __cdecl SseLoad(Rs7Mid::Context& c) {
    g_depth = c.FpuDepth();
    g_otherEdits = c.HasSt0() || c.SetSt0(1.0) || c.PushSt0(1.0);
    g_loaded = Rs7Mid::LoadSt0(c, 2.0f * g_a - g_b);
    _mm_setcsr(0x7f00);  // round toward zero: the thunk restores the caller's MXCSR
    g_clobberXmm();
}

void __cdecl SseNoop(Rs7Mid::Context&) {}

void __cdecl FullLoad(Rs7Mid::Context& c) {
    g_loaded = Rs7Mid::LoadSt0(c, 1.0f);
}

using Builder = size_t(__cdecl*)(uint8_t*, size_t, const uint8_t*, uintptr_t, size_t, uintptr_t, uintptr_t);

// A `nop x5; ret` site at the page start with its entry at +64.
uint8_t* Site(Rs7Mid::Callback callback, Builder build) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!page) {
        return nullptr;
    }

    const uint8_t raw[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};

    std::memcpy(page, raw, sizeof raw);

    if (!build(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), uintptr_t(callback))) {
        VirtualFree(page, 0, MEM_RELEASE);
        return nullptr;
    }

    return page;
}

#pragma warning(push)
#pragma warning(disable : 4324)  // padded to the alignas: fxsave needs 16-byte aligned images

struct alignas(16) Images {
    uint8_t original[512], before[512], after[512];
};

#pragma warning(pop)
}  // namespace

TEST(midhook_sse_loads_the_result_and_restores_sse_state) {
    uint8_t* page = Site(&SseLoad, &Rs7Mid_BuildSse);

    CHECK(page != nullptr);
    if (!page) {
        return;
    }

    static Images images{};
    static float loaded = 0, second = 0;
    static uint32_t entry = 0;
    Asm code;

    entry = uint32_t(uintptr_t(page + 64));
    code.Put({0x0f, 0xae, 0x05}, images.original);         // fxsave original
    code.Put({0xdb, 0xe3, 0xd9, 0xe8});                    // fninit; fld1
    code.Put({0x66, 0x0f, 0x76, 0xc0, 0x0f, 0x57, 0xff});  // pcmpeqd xmm0,xmm0; xorps xmm7,xmm7
    code.Put({0x0f, 0xae, 0x05}, images.before);
    code.Put({0xff, 0x15}, &entry);   // call [entry]
    code.Put({0xd9, 0x1d}, &loaded);  // fstp dword loaded
    code.Put({0x0f, 0xae, 0x05}, images.after);
    code.Put({0xd9, 0x1d}, &second);
    code.Put({0x0f, 0xae, 0x0d}, images.original);  // fxrstor original
    code.Put({0xc3});
    g_a = 3.25f, g_b = 0.5f;
    reinterpret_cast<void (*)()>(code.p)();

    CHECK(g_loaded);
    CHECK_EQ(g_depth, 0u);  // no FXSAVE image in sse mode: every x87 accessor fails
    CHECK(!g_otherEdits);
    CHECK_EQ(loaded, 6.0f);
    CHECK_EQ(second, 1.0f);
    // After popping the loaded value: control / status / tags, MXCSR (+24), ST0-ST6 and XMM as before the hook (the
    // empty ST7 slot keeps the popped value's bits).
    CHECK(std::memcmp(images.before, images.after, 5) == 0);
    CHECK(std::memcmp(images.before + 24, images.after + 24, 4) == 0);
    CHECK(std::memcmp(images.before + 32, images.after + 32, 7 * 16) == 0);
    CHECK(std::memcmp(images.before + 160, images.after + 160, 8 * 16) == 0);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST(midhook_load_st0_fails_outside_sse_mode) {
    for (const Builder build : {&Rs7Mid_Build, &Rs7Mid_BuildLight}) {
        uint8_t* page = Site(&FullLoad, build);

        CHECK(page != nullptr);
        if (!page) {
            return;
        }

        g_loaded = true;
        reinterpret_cast<void (*)()>(page + 64)();
        CHECK(!g_loaded);
        VirtualFree(page, 0, MEM_RELEASE);
    }
}

TEST(midhook_add32_matches_hardware_add) {
    const uint32_t inputs[] = {0, 1, 2, 0xf, 0x10, 0x7f, 0x80, 0x7fffffff, 0x80000000, 0xfffffff0, 0xffffffff};

    for (uint32_t lhsValue : inputs) {
        for (uint32_t rhsValue : inputs) {
            const uint32_t hardwareSum = g_hardwareAdd(lhsValue, rhsValue);
            Rs7Mid::Context context{};

            context.eflags = 0xed7;
            CHECK_EQ(context.Add32(lhsValue, rhsValue), hardwareSum);
            CHECK_EQ(context.eflags & 0x8d5, g_addFlags & 0x8d5);
            CHECK_EQ(context.eflags & ~0x8d5u, 0xed7u & ~0x8d5u);
        }
    }
}

namespace {
// A hand-built FXSAVE image with one valid register: ST0 = the 80-bit value (mantissa, sign | exponent).
struct Image {
    alignas(16) uint8_t bytes[512]{};

    Image(uint64_t mantissa, uint16_t top) {
        bytes[4] = 0x01;  // TOP 0, physical register 0 valid
        std::memcpy(bytes + 32, &mantissa, 8);
        std::memcpy(bytes + 40, &top, 2);
    }
};

int Order(const Image& image, float rhs) {
    Rs7Mid::Context c{};

    c.fpu = uint32_t(uintptr_t(image.bytes));

    return c.CompareSt0(rhs);
}

// What `fcomp dword` finds on the hardware: C0 C2 C3 -> -1 / 2 / 0 / 1.
int HardwareOrder(const uint8_t* extended, float rhs) {
    const uint32_t sw = g_fcomp(extended, rhs);
    const bool c0 = sw & 0x100, c2 = sw & 0x400, c3 = sw & 0x4000;

    return c2 ? 2 : c3 ? 0 : c0 ? -1 : 1;
}
}  // namespace

TEST(midhook_compare_st0_is_exact_and_matches_fcomp) {
    constexpr uint64_t k34 = 0x8800000000000000ull;  // 34.0 = 1.0001b * 2^5
    // 34, one 64-bit ulp above / below (both round to 34.0 as a double), -34, +0, -0, infinity, a quiet NaN, the
    // smallest denormal, an unnormal (invalid: unordered).
    constexpr uint64_t kTop = 1ull << 63;
    // 34, one 64-bit ulp above / below (both round to 34.0 as a double), -34, +0, -0, +inf, -inf, a quiet NaN, a
    // pseudo-infinity (no integer bit: invalid), the smallest denormal, a pseudo-denormal (integer bit, exponent 0:
    // equals the smallest normal), the smallest normal, an unnormal (invalid: unordered).
    const Image cases[] = {{k34, 0x4004}, {k34 + 1, 0x4004}, {k34 - 1, 0x4004}, {k34, 0xc004},        {0, 0},
                           {0, 0x8000},   {kTop, 0x7fff},    {kTop, 0xffff},    {3ull << 62, 0x7fff}, {0, 0x7fff},
                           {1, 0},        {kTop, 0},         {kTop, 1},         {1ull << 62, 0x4004}};
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    const float rhs[] = {34.0f, -34.0f, 0.0f, -0.0f, 1e-45f, 3.4e38f, 33.999996f, 34.000004f, inf, -inf, nan};

    for (const Image& image : cases) {
        for (const float r : rhs) {
            CHECK_EQ(Order(image, r), HardwareOrder(image.bytes + 32, r));
        }
    }

    CHECK_EQ(Order(cases[1], 34.0f), 1);  // exact: St0() would round it to 34.0
    CHECK_EQ(Order(cases[2], 34.0f), -1);
    CHECK_EQ(Order(cases[9], 34.0f), 2);  // pseudo-infinity
    CHECK_EQ(Order(cases[11], 0.0f), 1);  // pseudo-denormal: 2^-16382, above zero
    Rs7Mid::Context empty{};

    CHECK_EQ(empty.CompareSt0(1.0f), 2);  // no image (light mode)
}

TEST(midhook_relocates_fcomp_m32_only) {
    uint8_t out[64]{};
    // W 005B2DDE: fcomp dword [1224C6Ch] (D8 /3, absolute), and with a base register.
    const uint8_t abs[] = {0xd8, 0x1d, 0x6c, 0x4c, 0x22, 0x01}, based[] = {0xd8, 0x58, 0x10, 0x90, 0x90};

    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, abs, 0x5b2dde, sizeof abs, 0x2000), sizeof abs);
    CHECK(std::memcmp(out, abs, sizeof abs) == 0);
    CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, based, 0x1000, sizeof based, 0x2000), sizeof based);

    // The other D8 forms (fadd /0, fcom /2, fsub /4) and the register forms stay rejected.
    for (const uint8_t modrm : {uint8_t(0x05), uint8_t(0x15), uint8_t(0x25), uint8_t(0xd9)}) {
        const uint8_t other[] = {0xd8, modrm, 0x6c, 0x4c, 0x22, 0x01};

        CHECK_EQ(Rs7Mid_Relocate(out, sizeof out, other, 0x1000, sizeof other, 0x2000), 0u);
    }
}

// A full x87 stack (8 x fld1): the sse thunk itself pushes nothing, and its result load overflows exactly as the game's
// own `fld dword` would (compared with the hardware: status word and the register file).
TEST(midhook_sse_on_a_full_x87_stack_behaves_like_fld) {
    for (const bool load : {false, true}) {
        uint8_t* page = Site(load ? &SseLoad : &SseNoop, &Rs7Mid_BuildSse);

        CHECK(page != nullptr);
        if (!page) {
            return;
        }

        static Images images{};
        static uint16_t sw[2]{};
        static uint32_t entry = 0;
        static const float six = 6.0f;

        entry = uint32_t(uintptr_t(page + 64));

        for (int run = 0; run < 2; ++run) {  // run 0: the hook; run 1: the reference (`fld dword six` or nothing)
            Asm code;
            uint8_t* image = run ? images.after : images.before;

            code.Put({0x0f, 0xae, 0x05}, images.original);
            code.Put({0xdb, 0xe3});  // fninit

            for (int i = 0; i < 8; ++i) {
                code.Put({0xd9, 0xe8});  // fld1
            }

            if (run == 0) {
                code.Put({0xff, 0x15}, &entry);
            } else if (load) {
                code.Put({0xd9, 0x05}, &six);
            }

            code.Put({0xdd, 0x3d}, &sw[run]);  // fnstsw
            code.Put({0x0f, 0xae, 0x05}, image);
            code.Put({0xdb, 0xe2, 0x0f, 0xae, 0x0d}, images.original);  // fnclex; fxrstor original
            code.Put({0xc3});
            g_a = 3.25f, g_b = 0.5f;  // SseLoad loads 6.0
            reinterpret_cast<void (*)()>(code.p)();
        }

        CHECK_EQ(sw[0], sw[1]);
        CHECK(std::memcmp(images.before + 4, images.after + 4, 1) == 0);         // tags
        CHECK(std::memcmp(images.before + 32, images.after + 32, 8 * 16) == 0);  // ST0-ST7
        CHECK_EQ(load, (sw[0] & 0x41u) == 0x41u);  // the load: stack fault (SF | IE), as fld
        VirtualFree(page, 0, MEM_RELEASE);
    }
}
