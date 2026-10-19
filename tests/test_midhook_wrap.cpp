// The Wrap placement (Rs7Mid_BuildWrap, Hook::PrepareWrap; ARCHITECTURE.md section 15 "Wrap placement"): a caller
// `push A1; push A0; call callee; [add esp,8]; ret` runs stock and hooked on identical registers and flags. The callee
// counts its runs, returns A0 + A1 + ECX (flags from the ADD) in EAX and 5150h in EDX, scribbles over its first
// argument slot and cleans up with `ret 8` (stdcall) or `ret` (cdecl). A function-entry wrap runs a copy of a small
// framed function whose prologue is replayed.
#include "midhook_run.h"
#include "platform/midhook.h"
#include "test.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace Rs7MidRun;

uint32_t g_calleeRuns, g_callbacks, g_befores;
constexpr uint32_t kA0 = 0x11223344, kA1 = 0x01020304, kEdx = 0x5150;
Rs7Mid::Wrap g_wrap;  // the frame the callbacks read (set with the entry)

// page + 0: stock caller, + 32: hooked caller (its call at + 42), + 64: callee, + 128: the entry, + 448 / + 480: a
// framed function (stock / hooked copy whose first five bytes jump to the entry).
struct Page {
    uint8_t* p = nullptr;
    bool cdeclConv = false;

    explicit Page(bool cdeclCallee) : cdeclConv(cdeclCallee) {
        p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!p) {
            return;
        }

        Caller(0, Callee());
        Caller(32, Callee());
        const uint8_t callee[] = {0xff, 0x05, 0,    0,    0,    0,                  // inc [g_calleeRuns]
                                  0x8b, 0x44, 0x24, 0x04, 0x03, 0x44, 0x24, 0x08,   // eax = [esp+4] + [esp+8]
                                  0x03, 0xc1,                                       // add eax, ecx
                                  0xc7, 0x44, 0x24, 0x04, 0xef, 0xbe, 0xad, 0xde,   // mov [esp+4], DEADBEEFh
                                  0xba, 0x50, 0x51, 0,    0,    0xc2, 0x08, 0x00};  // mov edx, 5150h; ret 8

        std::memcpy(p + 64, callee, sizeof callee);
        Dword(p + 66, uint32_t(uintptr_t(&g_calleeRuns)));
        if (cdeclConv) {
            p[64 + sizeof callee - 3] = 0xc3;
        }

        // push ebp; mov ebp,esp; sub esp,8 | mov eax,[ebp+8]; add eax,[ebp+0Ch]; add eax,ecx; mov esp,ebp; pop ebp;
        // ret 8 (ret for cdecl)
        const uint8_t framed[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x8b, 0x45, 0x08, 0x03,
                                  0x45, 0x0c, 0x03, 0xc1, 0x8b, 0xe5, 0x5d, 0xc2, 0x08, 0x00};

        std::memcpy(p + 448, framed, sizeof framed);
        std::memcpy(p + 480, framed, sizeof framed);
        if (cdeclConv) {
            p[448 + sizeof framed - 3] = p[480 + sizeof framed - 3] = 0xc3;
        }
    }

    ~Page() {
        if (p) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }

    void Caller(size_t at, uintptr_t target) {
        p[at] = 0x68;
        Dword(p + at + 1, kA1);
        p[at + 5] = 0x68;
        Dword(p + at + 6, kA0);
        p[at + 10] = 0xe8;
        Dword(p + at + 11, uint32_t(target - uintptr_t(p + at + 15)));
        const uint8_t tail[] = {0x83, 0xc4, 0x08, 0xc3};  // add esp,8 (cdecl); ret

        std::memcpy(p + at + 15, cdeclConv ? tail : tail + 3, cdeclConv ? 4 : 1);
    }

    uintptr_t Stock() const { return uintptr_t(p); }

    uintptr_t Hooked() const { return uintptr_t(p + 32); }

    uintptr_t Site() const { return uintptr_t(p + 42); }

    uintptr_t Callee() const { return uintptr_t(p + 64); }

    uintptr_t Entry() const { return uintptr_t(p + 128); }

    Rs7Mid::Wrap Spec(uint16_t locals = 0) const {
        return {2, cdeclConv ? Rs7Mid::Conv::Cdecl : Rs7Mid::Conv::Stdcall, locals};
    }

    // The entry for the hooked caller's call (call site) or for the hooked framed function (entry), and the callers.
    bool Hook(Rs7Mid::Callback before, Rs7Mid::Callback after, bool light, bool entry = false, uint16_t locals = 0) {
        g_wrap = Spec(locals);
        const size_t n = Rs7Mid_BuildWrap(p + 128, 256, Entry(), entry ? 0 : Callee(), entry ? p + 448 : nullptr,
                                          uintptr_t(p + 480), 6, uintptr_t(before), uintptr_t(after), g_wrap.args,
                                          g_wrap.PopBytes(), g_wrap.locals, light ? 1 : 0);

        Caller(0, entry ? uintptr_t(p + 448) : Callee());
        Caller(32, entry ? uintptr_t(p + 480) : Entry());

        if (entry) {
            p[480] = 0xe9;
            Dword(p + 481, uint32_t(Entry() - uintptr_t(p + 485)));
        }

        return n > 0;
    }
};

struct Seen {
    Rs7Mid::Context c;
    Rs7Mid::WrapFrame frame;
    uint32_t arg0, arg1;
} g_after, g_before;

Seen Observe(const Rs7Mid::Context& c) {
    Rs7Mid::WrapFrame& f = Rs7Mid::Frame(c, g_wrap);

    return {c, f, f.Arg(0), f.Arg(1)};
}

void __cdecl After(Rs7Mid::Context& c) {
    g_after = Observe(c);
    ++g_callbacks;
}

void __cdecl Double(Rs7Mid::Context& c) {
    After(c);
    c.eax *= 2;
}

void __cdecl Before(Rs7Mid::Context& c) {
    g_before = Observe(c);
    ++g_befores;
}

void __cdecl BeforeEdits(Rs7Mid::Context& c) {
    Before(c);
    Rs7Mid::Frame(c, g_wrap).user = 0x55;
    Rs7Mid::Frame(c, g_wrap).Arg(1) = 1000;
    c.ecx = 100;
}

void __cdecl BeforeSkips(Rs7Mid::Context& c) {
    Before(c);
    Rs7Mid::WrapReturn(c, g_wrap, 77);
}

// Builds argument 0 in the scratch: a pointer to it (the callee adds the pointer value), with a marker behind it.
void __cdecl BeforeBuilds(Rs7Mid::Context& c) {
    Before(c);
    uint8_t* locals = Rs7Mid::Locals(c);

    Dword(locals, 0xc0ffee);
    Rs7Mid::Frame(c, g_wrap).Arg(0) = uint32_t(uintptr_t(locals));
}

uint32_t g_localsAfter;

void __cdecl AfterReadsLocals(Rs7Mid::Context& c) {
    After(c);
    std::memcpy(&g_localsAfter, Rs7Mid::Locals(c), 4);
}

void __cdecl ClobberFp(Rs7Mid::Context& c) {
    After(c);
    reinterpret_cast<void (*)()>(Helpers().clobber)();
}

void __cdecl HalveSt0(Rs7Mid::Context& c) {
    ClobberFp(c);
    CHECK(c.HasSt0() && c.St0() == 1.0);
    CHECK(c.SetSt0(0.5));
}

void Reset() {
    g_calleeRuns = g_callbacks = g_befores = 0;
    g_after = g_before = {};
}
}  // namespace

TEST(midhook_wrap_build_rejections) {
    uint8_t out[256]{};
    const auto build = [&](size_t capacity, uintptr_t before, uintptr_t after, uint32_t args, uint32_t pops,
                           uint32_t locals) {
        return Rs7Mid_BuildWrap(out, capacity, 0x3000, 0x2000, nullptr, 0x1000, 5, before, after, args, pops, locals,
                                0);
    };

    CHECK(build(sizeof out, 0, 0x4000, 2, 8, 0) > 0);
    CHECK(build(sizeof out, 0x4000, 0, 2, 0, 16) > 0);
    CHECK_EQ(build(sizeof out, 0, 0, 2, 8, 0), 0u);  // no callback
    CHECK_EQ(build(sizeof out, 0, 0x4000, Rs7Mid::kWrapMaxArgs + 1, 0, 0), 0u);
    CHECK_EQ(build(sizeof out, 0, 0x4000, 2, 4, 0), 0u);  // pops neither 0 nor 4 * args
    CHECK_EQ(build(sizeof out, 0, 0x4000, 2, 8, 6), 0u);  // locals not dword-sized
    CHECK_EQ(build(sizeof out, 0, 0x4000, 2, 8, Rs7Mid::kWrapMaxLocals + 4), 0u);
    CHECK_EQ(build(20, 0, 0x4000, 2, 8, 0), 0u);  // capacity
    CHECK_EQ(Rs7Mid_BuildWrap(out, sizeof out, 0x3000, 0, nullptr, 0x1000, 5, 0, 0x4000, 0, 0, 0, 0), 0u);
    const size_t maximal = build(sizeof out, 0x4000, 0x4000, Rs7Mid::kWrapMaxArgs, 0, Rs7Mid::kWrapMaxLocals);

    CHECK(maximal > 0 && maximal <= 200);  // Hook::Emit's 256 bytes hold the largest entry and a replay
    Rs7Mid::Hook hook;
    const uint8_t jmp[] = {0xe9, 0, 0, 0, 0};

    CHECK(!hook.PrepareWrap(0x1000, jmp, sizeof jmp, true, {1}, nullptr, &After));
    CHECK(!hook.PrepareWrap(0x1000, jmp, sizeof jmp, true, {1}, nullptr, nullptr));
    CHECK_EQ(hook.Entry(), 0u);
}

TEST(midhook_wrap_after_sees_the_original_arguments_and_matches_stock) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            Page page(cdeclConv);

            CHECK(page.p && page.Hook(nullptr, &After, light));

            for (const uint32_t flags : kFlagPatterns) {
                const Regs stock = Run(page.Stock(), flags);

                Reset();
                CHECK(Same(Run(page.Hooked(), flags), stock));
                CHECK_EQ(stock.eax, kA0 + kA1 + 5);
                CHECK_EQ(stock.edx, kEdx);
                CHECK_EQ(stock.espDelta, 0u);
                CHECK_EQ(g_calleeRuns, 1u);
                CHECK_EQ(g_callbacks, 1u);
                CHECK_EQ(g_after.arg0, kA0);  // not the callee's DEADBEEF: it ran on a copy
                CHECK_EQ(g_after.arg1, kA1);
                CHECK_EQ(g_after.frame.ret, page.Site() + 5);
                CHECK(g_after.frame.user == 0 && g_after.frame.eax == 0x1111 && g_after.frame.ecx == 5);
                CHECK_EQ(g_after.frame.edx, 0x4444u);
                CHECK_EQ(g_after.c.eax, kA0 + kA1 + 5);  // the callee's result, flags and EDX
                CHECK_EQ(g_after.c.edx, kEdx);
                CHECK(cdeclConv || g_after.c.eflags == stock.flags);  // cdecl: the caller's ADD comes later
            }
        }
    }
}

TEST(midhook_wrap_before_edits_registers_arguments_and_user) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            Page page(cdeclConv);

            CHECK(page.p && page.Hook(&BeforeEdits, &After, light));
            Reset();
            const Regs r = Run(page.Hooked(), 0x2d7);

            CHECK_EQ(g_befores, 1u);
            CHECK_EQ(g_before.arg0, kA0);
            CHECK_EQ(g_before.frame.ret, page.Site() + 5);
            CHECK_EQ(g_before.c.eflags & 0xfff, 0x2d7u);
            CHECK_EQ(g_before.c.ecx, 5u);
            CHECK_EQ(r.eax, kA0 + 1000 + 100);  // the callee got the edited argument and ECX
            CHECK_EQ(g_after.arg1, 1000u);      // the edit is in the caller's slot, which After reads
            CHECK_EQ(g_after.frame.user, 0x55u);
            CHECK_EQ(g_after.frame.ecx, 5u);  // the frame keeps the registers as the game called
            CHECK_EQ(r.espDelta, 0u);
        }
    }
}

TEST(midhook_wrap_before_skips_the_callee) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            for (const uint32_t flags : kFlagPatterns) {
                Page page(cdeclConv);

                CHECK(page.p && page.Hook(&BeforeSkips, &After, light));
                Regs stock = Run(page.Stock(), flags);

                Reset();
                const Regs r = Run(page.Hooked(), flags);

                CHECK_EQ(g_calleeRuns, 0u);
                CHECK_EQ(g_callbacks, 0u);  // no after callback either
                stock.eax = 77;
                stock.edx = 0x4444;  // the callee's EDX never happened
                if (!cdeclConv) {
                    stock.flags = flags;  // cdecl: both end with the caller's `add esp, 8`
                }

                CHECK(Same(r, stock));  // the cleanup of `ret 8` / the caller's `add esp, 8` balanced
            }
        }
    }
}

TEST(midhook_wrap_after_replaces_the_return_value) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            Page page(cdeclConv);

            CHECK(page.p && page.Hook(nullptr, &Double, light));
            Regs stock = Run(page.Stock(), 0x202);
            const Regs r = Run(page.Hooked(), 0x202);

            stock.eax *= 2;
            CHECK(Same(r, stock));
        }
    }
}

TEST(midhook_wrap_before_builds_an_argument_in_the_scratch) {
    for (const bool light : {false, true}) {
        Page page(false);

        CHECK(page.p && page.Hook(&BeforeBuilds, &AfterReadsLocals, light, false, 32));
        Reset();
        const Regs r = Run(page.Hooked(), 0x202);
        const uint32_t locals = uint32_t(uintptr_t(Rs7Mid::Locals(g_before.c)));

        CHECK_EQ(r.eax, locals + kA1 + 5);
        CHECK_EQ(g_after.c.esp, g_before.c.esp);  // the same frame, the scratch intact
        CHECK_EQ(g_localsAfter, 0xc0ffeeu);
        CHECK_EQ(g_after.arg0, locals);
        CHECK_EQ(g_after.frame.ret, page.Site() + 5);
        CHECK_EQ(r.espDelta, 0u);
    }
}

TEST(midhook_wrap_function_entry_replays_the_prologue) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            Page page(cdeclConv);

            CHECK(page.p && page.Hook(nullptr, &After, light, true));

            for (const uint32_t flags : kFlagPatterns) {
                const Regs stock = Run(page.Stock(), flags);

                Reset();
                CHECK(Same(Run(page.Hooked(), flags), stock));
                CHECK_EQ(stock.eax, kA0 + kA1 + 5);
                CHECK_EQ(g_callbacks, 1u);
                CHECK(g_after.arg0 == kA0 && g_after.arg1 == kA1);
            }
        }
    }
}

TEST(midhook_wrap_after_reads_and_replaces_an_x87_result_in_full_mode) {
    Page page(false);

    CHECK(page.p != nullptr);
    if (!page.p) {
        return;
    }

    const uint8_t fld1[] = {0xd9, 0xe8, 0xc2, 0x08, 0x00};  // a stdcall callee returning 1.0 in ST0

    std::memcpy(page.p + 64, fld1, sizeof fld1);
    CHECK(page.Hook(nullptr, &HalveSt0, false));
    Reset();
    const auto caller = reinterpret_cast<float(__stdcall*)(uint32_t, uint32_t)>(page.Entry());
    const float value = caller(kA0, kA1);

    CHECK_EQ(g_callbacks, 1u);
    CHECK(value == 0.5f);  // SetSt0 reached the resumed state, the callback's FNINIT / XORPS did not
}

// Scratch of 128 and 256 bytes: the frame's `lea esp` and the argument copies' `push [esp + d]` take their 32-bit
// displacement forms (d = locals + 16 + 4 * args > 127).
TEST(midhook_wrap_large_scratch_uses_the_disp32_forms) {
    for (const uint16_t locals : {uint16_t(128), uint16_t(256)}) {
        for (const bool cdeclConv : {false, true}) {
            for (const bool light : {false, true}) {
                Page page(cdeclConv);

                CHECK(page.p && page.Hook(&Before, &After, light, false, locals));
                const uint8_t* entry = page.p + 128;

                CHECK(locals == 128 || (entry[5] == 0x8d && entry[6] == 0xa4));  // lea esp, [esp - 256]: disp32
                const Regs stock = Run(page.Stock(), 0x2d7);

                Reset();
                CHECK(Same(Run(page.Hooked(), 0x2d7), stock));
                CHECK_EQ(stock.espDelta, 0u);
                CHECK(g_before.arg0 == kA0 && g_before.arg1 == kA1);
                CHECK(g_after.arg0 == kA0 && g_after.arg1 == kA1);
                CHECK_EQ(g_after.frame.ret, page.Site() + 5);
                CHECK_EQ(g_after.c.esp, g_before.c.esp);
                CHECK_EQ(g_calleeRuns, 1u);
            }
        }
    }
}

// No stack arguments, plain `ret`: the callee `lea eax,[ecx+1]; ret` called by `call; ret`.
TEST(midhook_wrap_without_arguments_returns_with_a_plain_ret) {
    for (const bool light : {false, true}) {
        Page page(false);

        CHECK(page.p != nullptr);
        if (!page.p) {
            continue;
        }

        const uint8_t callee[] = {0x8d, 0x41, 0x01, 0xc3};

        std::memcpy(page.p + 400, callee, sizeof callee);
        page.p[0] = page.p[32] = 0xe8;
        Dword(page.p + 1, uint32_t(uintptr_t(page.p + 400) - uintptr_t(page.p + 5)));
        Dword(page.p + 33, uint32_t(page.Entry() - uintptr_t(page.p + 37)));
        page.p[5] = page.p[37] = 0xc3;
        g_wrap = {0, Rs7Mid::Conv::Stdcall};
        const size_t n = Rs7Mid_BuildWrap(page.p + 128, 256, page.Entry(), uintptr_t(page.p + 400), nullptr,
                                          uintptr_t(page.p + 32), 5, 0, uintptr_t(&Double), 0, 0, 0, light ? 1 : 0);

        CHECK(n > 0 && page.p[128 + n - 1] == 0xc3);  // the entry ends with a plain `ret`
        Regs stock = Run(page.Stock(), 0x202);

        Reset();
        const Regs r = Run(page.Hooked(), 0x202);

        CHECK_EQ(stock.eax, 6u);
        CHECK_EQ(g_callbacks, 1u);
        CHECK_EQ(g_after.frame.ret, uintptr_t(page.p + 37));
        stock.eax *= 2;
        CHECK(Same(r, stock));
    }
}

// Only a before callback (AM1's shape): the entry continues from the callee straight to its tail, no second thunk pass.
TEST(midhook_wrap_before_only_has_no_after_pass) {
    for (const bool cdeclConv : {false, true}) {
        for (const bool light : {false, true}) {
            Page page(cdeclConv);

            CHECK(page.p && page.Hook(&BeforeEdits, nullptr, light));
            Reset();
            const Regs r = Run(page.Hooked(), 0x202);

            CHECK_EQ(g_befores, 1u);
            CHECK_EQ(g_callbacks, 0u);
            CHECK_EQ(g_calleeRuns, 1u);
            CHECK_EQ(r.eax, kA0 + 1000 + 100);  // the edited argument and ECX reached the callee
            CHECK_EQ(r.edx, kEdx);              // the callee's registers come back unchanged
            CHECK_EQ(r.espDelta, 0u);
        }
    }
}
