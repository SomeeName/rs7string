// Call-site mid-hooks (Rs7Mid_BuildCall, Hook::PrepareCall; ARCHITECTURE.md section 15 "Call sites"): a small caller
// `push imm32; call callee; ret` runs stock and hooked on identical registers and flags. The callee is
// `inc [g_calleeRuns]; mov eax,[esp+4]; add eax,ecx; ret 4` (stdcall-like cleanup, flags from the ADD).
#include "midhook_run.h"
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <float.h>

namespace {
using namespace Rs7MidRun;

uint32_t g_calleeRuns;
constexpr uint32_t kArg = 0x11223344;

// page + 0: stock caller, + 16: hooked caller (site at + 21), + 32: callee, + 128: the entry.
struct Page {
    uint8_t* p = nullptr;

    Page() {
        p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!p) {
            return;
        }

        Caller(0, Callee());
        Caller(16, Callee());
        const uint8_t callee[] = {0xff, 0x05, 0, 0, 0, 0, 0x8b, 0x44, 0x24, 0x04, 0x03, 0xc1, 0xc2, 0x04, 0x00};

        std::memcpy(p + 32, callee, sizeof callee);
        Dword(p + 34, uint32_t(uintptr_t(&g_calleeRuns)));
    }

    ~Page() {
        if (p) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }

    void Caller(size_t at, uintptr_t target) {
        p[at] = 0x68;
        Dword(p + at + 1, kArg);
        p[at + 5] = 0xe8;
        Dword(p + at + 6, uint32_t(target - uintptr_t(p + at + 10)));
        p[at + 10] = 0xc3;
    }

    uintptr_t Stock() const { return uintptr_t(p); }

    uintptr_t Hooked() const { return uintptr_t(p + 16); }

    uintptr_t Site() const { return uintptr_t(p + 21); }

    uintptr_t Callee() const { return uintptr_t(p + 32); }

    uintptr_t Entry() const { return uintptr_t(p + 128); }

    // Builds the entry for the hooked caller's call and retargets that call to it.
    bool Hook(Rs7Mid::Callback callback, bool light, bool after) {
        const size_t n = Rs7Mid_BuildCall(p + 128, 256, Site(), Callee(), Entry(), uintptr_t(callback), light ? 1 : 0,
                                          after ? 1 : 0);

        Caller(16, Entry());

        return n == (after ? 24u : 15u);
    }
};

Rs7Mid::Context g_seen;
uint32_t g_seenArg, g_seenReturn, g_callbacks;

void __cdecl Record(Rs7Mid::Context& c) {
    g_seen = c;
    g_seenReturn = Rs7Mid::Dword(c.esp);
    g_seenArg = Rs7Mid::Dword(c.esp + 4);
    ++g_callbacks;
}

void __cdecl Skip(Rs7Mid::Context& c) {
    Record(c);
    Rs7Mid::Return(c, 77, 4);
}

void __cdecl EditEcx(Rs7Mid::Context& c) {
    Record(c);
    c.ecx = 100;
}

void __cdecl Double(Rs7Mid::Context& c) {
    Record(c);
    c.eax *= 2;
}

void __cdecl ClobberFp(Rs7Mid::Context& c) {
    Record(c);
    reinterpret_cast<void (*)()>(Helpers().clobber)();
}
}  // namespace

TEST(midhook_call_build_sizes_and_rejections) {
    uint8_t out[64]{};

    CHECK_EQ(Rs7Mid_BuildCall(out, sizeof out, 0x1000, 0x2000, 0x3000, 0x4000, 0, 0), 15u);
    CHECK_EQ(out[0], 0x68);
    CHECK_EQ(Rs7Mid_BuildCall(out, sizeof out, 0x1000, 0x2000, 0x3000, 0x4000, 1, 1), 24u);
    CHECK_EQ(out[0], 0x8d);
    CHECK_EQ(out[4], 0xe8);
    CHECK_EQ(Rs7Mid_BuildCall(out, 23, 0x1000, 0x2000, 0x3000, 0x4000, 0, 1), 0u);
    CHECK_EQ(Rs7Mid_BuildCall(out, sizeof out, 0x1000, 0, 0x3000, 0x4000, 0, 0), 0u);
    CHECK_EQ(Rs7Mid_BuildCall(out, sizeof out, 0x1000, 0x2000, 0x3000, 0, 0, 0), 0u);
    Rs7Mid::Hook hook;
    const uint8_t jmp[] = {0xe9, 0, 0, 0, 0};
    const uint8_t call[] = {0xe8, 0xfb, 0x0f, 0, 0};

    CHECK(!hook.PrepareCall(0x1000, jmp, sizeof jmp, &Record));
    CHECK(!hook.PrepareCall(0x1000, call, 4, &Record));
    CHECK_EQ(hook.Entry(), 0u);
    CHECK(hook.PrepareCall(0x1000, call, sizeof call, &Record, Rs7Mid::Mode::Light, Rs7Mid::Call::After));
    CHECK(hook.Entry() != 0);
    int32_t rel;

    std::memcpy(&rel, reinterpret_cast<const uint8_t*>(hook.Entry()) + 5, 4);
    CHECK_EQ(hook.Entry() + 9 + rel, 0x2000u);  // the callee decoded from the site bytes
}

TEST(midhook_call_noop_matches_stock) {
    for (const bool light : {false, true}) {
        for (const bool after : {false, true}) {
            Page page;

            CHECK(page.p && page.Hook(&Record, light, after));

            for (const uint32_t flags : kFlagPatterns) {
                const Regs stock = Run(page.Stock(), flags);

                g_calleeRuns = g_callbacks = 0;
                CHECK(Same(Run(page.Hooked(), flags), stock));
                CHECK_EQ(g_calleeRuns, 1u);
                CHECK_EQ(g_callbacks, 1u);
                CHECK_EQ(stock.eax, kArg + 5);
                CHECK_EQ(stock.espDelta, 0u);
            }
        }
    }
}

TEST(midhook_call_before_sees_the_callee_entry_and_edits_arguments) {
    for (const bool light : {false, true}) {
        Page page;

        CHECK(page.p && page.Hook(&EditEcx, light, false));
        const Regs r = Run(page.Hooked(), 0x2d7);

        CHECK_EQ(g_seen.jump, page.Callee());
        CHECK_EQ(g_seenReturn, page.Site() + 5);  // the game's own return address
        CHECK_EQ(g_seenArg, kArg);
        CHECK_EQ(g_seen.ecx, 5u);
        CHECK_EQ(g_seen.eflags & 0xfff, 0x2d7u);
        CHECK_EQ(r.eax, kArg + 100);
        CHECK_EQ(r.ecx, 100u);
    }
}

TEST(midhook_call_before_skips_the_callee) {
    for (const bool light : {false, true}) {
        for (const uint32_t flags : kFlagPatterns) {
            Page page;

            CHECK(page.p && page.Hook(&Skip, light, false));
            g_calleeRuns = 0;
            Regs stock = Run(page.Stock(), flags);
            const Regs r = Run(page.Hooked(), flags);

            CHECK_EQ(g_calleeRuns, 1u);  // the stock run only
            CHECK_EQ(r.eax, 77u);
            CHECK_EQ(r.flags, flags);  // the callback left the incoming flags (no ADD ran)
            stock.eax = 77;
            stock.flags = flags;
            CHECK(Same(r, stock));  // `ret 4` cleanup: the stack is balanced as after the callee
        }
    }
}

TEST(midhook_call_after_replaces_the_return_value) {
    for (const bool light : {false, true}) {
        for (const uint32_t flags : kFlagPatterns) {
            Page page;

            CHECK(page.p && page.Hook(&Record, light, false));
            Run(page.Hooked(), flags);
            const uint32_t atEntry = g_seen.esp;

            CHECK(page.Hook(&Double, light, true));
            Regs stock = Run(page.Stock(), flags);

            g_calleeRuns = 0;
            const Regs r = Run(page.Hooked(), flags);

            CHECK_EQ(g_calleeRuns, 1u);
            CHECK_EQ(g_seen.jump, page.Site() + 5);
            CHECK_EQ(g_seen.eax, kArg + 5);
            CHECK_EQ(g_seen.eflags, stock.flags);  // the callee's ADD flags
            CHECK_EQ(g_seen.esp, atEntry + 8);     // past the return address and the `ret 4` argument
            stock.eax = 2 * (kArg + 5);
            CHECK(Same(r, stock));
        }
    }
}

TEST(midhook_call_after_keeps_an_x87_return_value_in_full_mode) {
    Page page;

    CHECK(page.p != nullptr);
    if (!page.p) {
        return;
    }

    const uint8_t fld1[] = {0xd9, 0xe8, 0xc3};  // a callee returning 1.0 in ST0

    std::memcpy(page.p + 64, fld1, sizeof fld1);
    page.p[48] = 0xe8;
    Dword(page.p + 49, uint32_t(page.Entry() - uintptr_t(page.p + 53)));
    page.p[53] = 0xc3;
    CHECK_EQ(Rs7Mid_BuildCall(page.p + 128, 256, uintptr_t(page.p + 48), uintptr_t(page.p + 64), page.Entry(),
                              uintptr_t(&ClobberFp), 0, 1),
             24u);
    g_callbacks = 0;
    const float value = reinterpret_cast<float(__cdecl*)()>(page.p + 48)();

    CHECK_EQ(g_callbacks, 1u);
    CHECK(value == 1.0f);  // the callback's FNINIT did not reach the resumed state
}

namespace {
// A call-in callback in place of the callee: EAX = the argument + ECX, as the callee computes it.
void __cdecl AddArg(Rs7Mid::Context& c) {
    Record(c);
    c.eax = Rs7Mid::Dword(c.esp + 4) + c.ecx;
}
}  // namespace

// Call-in entries (Rs7Mid::CallIn; ARCHITECTURE.md section 15 "Call-in entries"): the call entry with a `ret N`
// continuation, no callee.
TEST(midhook_callin_build_sizes_and_rejections) {
    uint8_t out[32]{};

    CHECK_EQ(Rs7Mid_BuildCallIn(out, sizeof out, 0x3000, 0x4000, 0, 0), 16u);
    CHECK_EQ(out[15], 0xc3);
    CHECK_EQ(Rs7Mid_BuildCallIn(out, sizeof out, 0x3000, 0x4000, 1, 12), 18u);
    CHECK_EQ(out[15], 0xc2);
    CHECK_EQ(out[16], 12);
    CHECK_EQ(out[17], 0);
    CHECK_EQ(Rs7Mid_BuildCallIn(out, 17, 0x3000, 0x4000, 0, 4), 0u);
    CHECK_EQ(Rs7Mid_BuildCallIn(out, sizeof out, 0x3000, 0x4000, 0, 6), 0u);
    CHECK_EQ(Rs7Mid_BuildCallIn(out, sizeof out, 0x3000, 0, 0, 0), 0u);
    CHECK_EQ(Rs7Mid_BuildCallIn(nullptr, sizeof out, 0x3000, 0x4000, 0, 0), 0u);
}

TEST(midhook_callin_returns_to_the_caller_with_the_edited_registers) {
    for (const auto mode : {Rs7Mid::Mode::Full, Rs7Mid::Mode::Light}) {
        Page page;
        Rs7Mid::CallIn callIn{&AddArg, mode, 4};

        CHECK(page.p && callIn.Entry() != 0);
        CHECK_EQ(callIn.Entry(), callIn.Entry());  // built once
        page.Caller(16, callIn.Entry());

        for (const uint32_t flags : kFlagPatterns) {
            Regs stock = Run(page.Stock(), flags);

            g_calleeRuns = g_callbacks = 0;
            const Regs r = Run(page.Hooked(), flags);

            CHECK_EQ(g_callbacks, 1u);
            CHECK_EQ(g_calleeRuns, 0u);  // no callee: the callback is the call
            CHECK_EQ(g_seenReturn, page.Site() + 5);
            CHECK_EQ(r.flags, flags);  // the callback left the incoming flags (the callee's ADD did not run)
            stock.flags = flags;
            CHECK(Same(r, stock));  // `ret 4`: the stack is balanced as after the callee
        }
    }
}

namespace {
unsigned g_pcInside, g_pcOutside;

// Inside Rs7Mid::GameFcw: the caller's precision control; outside it: the thunk's (FNINIT, 64-bit).
void __cdecl SeeFcw(Rs7Mid::Context& c) {
    {
        const Rs7Mid::GameFcw fcw(c);

        g_pcInside = _control87(0, 0) & _MCW_PC;
    }

    g_pcOutside = _control87(0, 0) & _MCW_PC;
}
}  // namespace

TEST(midhook_game_fcw_runs_game_code_with_the_callers_control_word) {
    Page page;
    Rs7Mid::CallIn callIn{&SeeFcw, Rs7Mid::Mode::Full, 4};

    CHECK(page.p && callIn.Entry() != 0);
    page.Caller(16, callIn.Entry());
    Rs7Mid_Fldcw(0x27F);  // the caller: 53-bit precision
    Run(page.Hooked(), 0x202);

    const unsigned after = _control87(0, 0) & _MCW_PC;

    Rs7Mid_Fldcw(0x27F);
    CHECK_EQ(g_pcInside, unsigned(_PC_53));
    CHECK_EQ(g_pcOutside, unsigned(_PC_64));
    CHECK_EQ(after, unsigned(_PC_53));  // FXRSTOR gave the caller its own back
}
