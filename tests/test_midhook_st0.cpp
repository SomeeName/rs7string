// Context::St0 / SetSt0 / PushSt0 / FpuDepth: the saved x87 stack of a full-mode mid-hook (ARCHITECTURE.md section 15,
// "x87 ST0"). Each test runs a real entry (Rs7Mid_Build / Rs7Mid_BuildLight) over a `nop x5; ret` site, called from a
// small machine-code driver that sets up the x87 stack and records it around the call.
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
uint64_t Bits(double value) {
    uint64_t bits;

    std::memcpy(&bits, &value, sizeof bits);

    return bits;
}

double FromBits(uint64_t bits) {
    double value;

    std::memcpy(&value, &bits, sizeof value);

    return value;
}

unsigned Popcount(unsigned v) {
    unsigned n = 0;

    for (; v; v &= v - 1) {
        ++n;
    }

    return n;
}

struct Seen {
    double st0 = 0;
    bool has = false, set = false, pushed = false;
    unsigned depth = 99;
    uint32_t fpu = 1;
};

Seen g_seen;
double g_value = 0;
int g_pushes = 0;

void __cdecl Replace(Rs7Mid::Context& c) {
    g_seen.fpu = c.fpu;
    g_seen.depth = c.FpuDepth();
    g_seen.has = c.HasSt0();
    g_seen.st0 = c.St0();
    g_seen.set = c.SetSt0(g_value);
}

// Light mode: integer only (no double arguments or results, which would use the game's x87 / SSE registers).
void __cdecl LightProbe(Rs7Mid::Context& c) {
    g_seen.fpu = c.fpu;
    g_seen.depth = c.FpuDepth();
    g_seen.has = c.HasSt0();
}

void __cdecl Push(Rs7Mid::Context& c) {
    g_seen.fpu = c.fpu;

    for (int i = 0; i < g_pushes; ++i) {
        g_seen.pushed = c.PushSt0(g_value + i);
    }

    g_seen.depth = c.FpuDepth();
}

// What the driver records: x87 images and status words before / after the hook, and the values popped after it.
#pragma warning(push)
#pragma warning(disable : 4324)  // padded to the alignas: fxsave needs 16-byte aligned images

struct alignas(16) Record {
    uint8_t saved[512], before[512], after[512];
    uint16_t swBefore, swAfter;
    double in[8], out[8];
};

#pragma warning(pop)

// A `nop x5; ret` site with a mid-hook entry, and a driver: fxsave saved; fninit; loads; fnstsw/fxsave before; call
// the entry; fnstsw/fxsave after; `pops` x fstp qword out[i]; fxrstor saved; ret. A load is a double from in[i]
// (fld qword) or an x87 constant opcode pair (fld1 D9 E8, fldpi D9 EB).
class Site {
public:
    explicit Site(Rs7Mid::Callback callback, bool light = false) {
        page_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!page_) {
            return;
        }

        const uint8_t raw[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};
        const auto build = light ? &Rs7Mid_BuildLight : &Rs7Mid_Build;

        std::memcpy(page_, raw, sizeof raw);
        ok_ = build(page_ + 64, 256, page_, uintptr_t(page_), 5, uintptr_t(page_ + 64), uintptr_t(callback)) > 0;
        record_ = reinterpret_cast<Record*>(page_ + 4096);
    }

    ~Site() {
        if (page_) {
            VirtualFree(page_, 0, MEM_RELEASE);
        }
    }

    bool Ok() const { return ok_; }

    // loads: a double (pushed from in[]) per entry, or 1 / pi for the constants. Returns the record.
    const Record& Run(const std::vector<double>& loads, int pops, const std::vector<uint16_t>& constants = {}) {
        std::vector<uint8_t> code;

        Mem(code, {0x0f, 0xae, 0x05}, record_->saved);  // fxsave saved
        code.insert(code.end(), {0xdb, 0xe3});          // fninit

        for (size_t i = 0; i < loads.size(); ++i) {
            record_->in[i] = loads[i];
            Mem(code, {0xdd, 0x05}, &record_->in[i]);  // fld qword
        }

        for (const uint16_t op : constants) {
            code.insert(code.end(), {uint8_t(op >> 8), uint8_t(op)});
        }

        Mem(code, {0xdd, 0x3d}, &record_->swBefore);  // fnstsw word
        Mem(code, {0x0f, 0xae, 0x05}, record_->before);
        const uintptr_t at = uintptr_t(page_ + 512) + code.size();

        code.push_back(0xe8);  // call entry
        Dword(code, uint32_t(uintptr_t(page_ + 64) - (at + 5)));
        Mem(code, {0xdd, 0x3d}, &record_->swAfter);
        Mem(code, {0x0f, 0xae, 0x05}, record_->after);

        for (int i = 0; i < pops; ++i) {
            Mem(code, {0xdd, 0x1d}, &record_->out[i]);  // fstp qword
        }

        Mem(code, {0x0f, 0xae, 0x0d}, record_->saved);  // fxrstor saved
        code.push_back(0xc3);
        std::memcpy(page_ + 512, code.data(), code.size());
        reinterpret_cast<void (*)()>(page_ + 512)();

        return *record_;
    }

private:
    static void Dword(std::vector<uint8_t>& code, uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            code.push_back(uint8_t(v >> (8 * i)));
        }
    }

    static void Mem(std::vector<uint8_t>& code, std::initializer_list<uint8_t> op, const void* address) {
        code.insert(code.end(), op);
        Dword(code, uint32_t(uintptr_t(address)));
    }

    uint8_t* page_ = nullptr;
    Record* record_ = nullptr;
    bool ok_ = false;
};

unsigned Top(uint16_t sw) {
    return (sw >> 11) & 7u;
}

constexpr uint16_t kFld1 = 0xd9e8, kFldPi = 0xd9eb;
}  // namespace

TEST(midhook_st0_round_trips_special_values) {
    Site site(&Replace);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    const double values[] = {
        0.0,
        -0.0,
        1.5,
        -3.25e300,
        DBL_MIN,
        DBL_MAX,
        FromBits(1),                      // smallest denormal
        FromBits(0x000fffffffffffffull),  // largest denormal
        FromBits(0x7ff0000000000000ull),  // +Inf
        FromBits(0xfff0000000000000ull),  // -Inf
        FromBits(0x7ff8000000001234ull),  // quiet NaN with a payload
        FromBits(0xfff8000000000000ull),  // the default NaN
    };

    for (const double read : values) {
        for (const double write : values) {
            g_seen = {};
            g_value = write;
            const Record& r = site.Run({read}, 1);

            CHECK(g_seen.fpu != 0);
            CHECK(g_seen.has);
            CHECK(g_seen.set);
            CHECK_EQ(g_seen.depth, 1u);
            CHECK_EQ(Bits(g_seen.st0), Bits(read));  // double -> 80 bits -> double is exact
            CHECK_EQ(Bits(r.out[0]), Bits(write));
            CHECK_EQ(Top(r.swAfter), Top(r.swBefore));  // FPU stack depth unchanged
            CHECK_EQ(r.after[4], r.before[4]);          // tag word unchanged
        }
    }
}

TEST(midhook_st0_writes_the_bits_fld_would) {
    Site site(&Replace);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    // SetSt0's 80-bit image equals what the hardware loads for the same double (the driver's fld of in[1]).
    for (const uint64_t bits : {0x3ff8000000000000ull, 0x0000000000000001ull, 0x8000000000000000ull,
                                0x7ff0000000000001ull /* signalling NaN: quieted, as fld does */}) {
        g_seen = {};
        g_value = FromBits(bits);
        const Record& r = site.Run({g_value, 7.0}, 2);

        CHECK(std::memcmp(r.after + 32, r.before + 48, 10) == 0);  // new ST0 == the fld of the same value (old ST1)
    }
}

TEST(midhook_st0_rounds_an_extended_value) {
    Site site(&Replace);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    g_value = 1.0;
    site.Run({}, 1, {kFldPi});                            // 64-bit pi, not a double
    CHECK_EQ(Bits(g_seen.st0), Bits(3.141592653589793));  // round to nearest

    // Rounding cases: an extended mantissa with the 11 dropped bits at, below and above half.
    struct Case {
        uint64_t mantissa;
        uint64_t expected;
    };

    const Case cases[] = {
        {0x8000000000000400ull, 0x3ff0000000000000ull},  // tie, even: down
        {0x8000000000000c00ull, 0x3ff0000000000002ull},  // tie, odd: up
        {0x80000000000003ffull, 0x3ff0000000000000ull},  // below half
        {0x8000000000000401ull, 0x3ff0000000000001ull},  // above half
        {0xfffffffffffffc00ull, 0x4000000000000000ull},  // carry into the exponent
    };
    Rs7Mid::Context c{};
    alignas(16) uint8_t image[512]{};

    c.fpu = uint32_t(uintptr_t(image));
    image[2] = 0, image[3] = 7 << 3;  // TOP = 7
    image[4] = 0x80;                  // physical 7 valid

    for (const Case& k : cases) {
        const uint16_t top = 0x3fff;

        std::memcpy(image + 32, &k.mantissa, 8);
        std::memcpy(image + 40, &top, 2);
        CHECK_EQ(Bits(c.St0()), k.expected);
    }
}

TEST(midhook_st0_on_an_empty_stack_changes_nothing) {
    Site site(&Replace);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    g_value = 2.0;
    const Record& r = site.Run({}, 0);

    CHECK(!g_seen.has);
    CHECK(!g_seen.set);
    CHECK_EQ(g_seen.depth, 0u);
    CHECK(g_seen.st0 != g_seen.st0);  // NaN
    CHECK_EQ(r.after[4], 0u);
    CHECK_EQ(Top(r.swAfter), 0u);
}

TEST(midhook_push_st0_is_an_fld) {
    Site site(&Push);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    g_value = 6.5;
    g_pushes = 1;
    const Record& r = site.Run({1.0, 2.0}, 3);

    CHECK(g_seen.pushed);
    CHECK_EQ(g_seen.depth, 3u);
    CHECK_EQ(r.out[0], 6.5);
    CHECK_EQ(r.out[1], 2.0);
    CHECK_EQ(r.out[2], 1.0);
    CHECK_EQ(Top(r.swAfter), (Top(r.swBefore) + 7) & 7u);
    CHECK_EQ(r.swAfter & 0x200u, 0u);                     // C1 cleared, as `fld` does
    CHECK_EQ(r.swAfter & 0xc5ffu, r.swBefore & 0xc5ffu);  // every other status bit kept
    CHECK_EQ(Popcount(r.after[4]), 3u);
}

TEST(midhook_push_st0_refuses_a_full_stack) {
    Site site(&Push);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    g_value = 5.0;
    g_pushes = 9;  // the first succeeds (7 -> 8 values), the others fail and change nothing
    const Record& r = site.Run({1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0}, 8);

    CHECK(!g_seen.pushed);
    CHECK_EQ(g_seen.depth, 8u);
    CHECK_EQ(Popcount(r.before[4]), 7u);
    CHECK_EQ(r.after[4], 0xffu);
    CHECK_EQ(r.out[0], 5.0);

    for (int i = 1; i < 8; ++i) {
        CHECK_EQ(r.out[i], 1.0);
    }
}

TEST(midhook_light_mode_has_no_fpu_image) {
    Site site(&LightProbe, true);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    const Record& r = site.Run({4.0}, 1);

    CHECK_EQ(g_seen.fpu, 0u);
    CHECK(!g_seen.has);  // so SetSt0 / PushSt0 fail without touching anything
    CHECK_EQ(g_seen.depth, 0u);
    CHECK_EQ(r.out[0], 4.0);  // the game's ST0 is untouched
    CHECK_EQ(Top(r.swAfter), Top(r.swBefore));
}

// PushSt0 on a hand-built image: TOP 0 wraps to 7 (an empty stack and one with physical register 0 in use).
TEST(midhook_push_st0_wraps_top_and_starts_an_empty_stack) {
    for (const uint8_t tags : {uint8_t(0x00), uint8_t(0x01)}) {
        alignas(16) uint8_t image[512]{};
        Rs7Mid::Context c{};

        c.fpu = uint32_t(uintptr_t(image));
        image[2] = 0x7f, image[3] = 0x02;  // FSW: TOP 0, C1 set, exception bits set
        image[4] = tags;
        image[32 + 9] = 0x3f, image[32 + 8] = 0xff, image[32 + 7] = 0x80;  // old ST0 = 1.0
        CHECK_EQ(c.HasSt0(), tags != 0);
        CHECK(c.PushSt0(-2.5));
        CHECK_EQ(Top(uint16_t(image[2] | image[3] << 8)), 7u);
        CHECK_EQ(image[3] & 0x02u, 0u);  // C1 cleared
        CHECK_EQ(image[2], 0x7fu);       // exception bits kept
        CHECK_EQ(image[4], uint8_t(tags | 0x80));
        CHECK_EQ(c.FpuDepth(), tags ? 2u : 1u);
        CHECK_EQ(c.St0(), -2.5);
        CHECK_EQ(image[48 + 9], 0x3fu);  // the old ST0 is ST1 now
    }
}

TEST(midhook_push_st0_on_an_empty_stack_through_the_thunk) {
    Site site(&Push);

    CHECK(site.Ok());
    if (!site.Ok()) {
        return;
    }

    g_seen = {};
    g_value = 3.0;
    g_pushes = 1;
    const Record& r = site.Run({}, 1);

    CHECK(g_seen.pushed);
    CHECK_EQ(g_seen.depth, 1u);
    CHECK_EQ(Top(r.swBefore), 0u);
    CHECK_EQ(Top(r.swAfter), 7u);  // fninit's TOP 0 wraps
    CHECK_EQ(r.after[4], 0x80u);
    CHECK_EQ(r.out[0], 3.0);
}
