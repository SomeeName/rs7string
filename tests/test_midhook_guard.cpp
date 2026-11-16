// Rs7Mid_Guard (platform/midhook.cpp): a callback that faults is skipped; the site resumes with the registers it had
// before the callback ran, and the fault is counted and reported.
#include "midhook_run.h"
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {
volatile uint32_t* g_null = nullptr;
uintptr_t g_loggedCallback = 0;
unsigned long g_loggedCode = 0;

// Edits every register, then reads through a null pointer.
void __cdecl EditThenFault(Rs7Mid::Context& c) {
    c.eax = 0xdead;
    c.ecx = 0xbeef;
    c.esi = 0;
    c.eflags ^= 1;
    c.eax += *g_null;
}

void __cdecl EditOnly(Rs7Mid::Context& c) {
    c.eax = 0x7777;
}

void __cdecl Record(uintptr_t callback, unsigned long code, long) {
    g_loggedCallback = callback;
    g_loggedCode = code;
}

// nop x5; ret, hooked at its first five bytes with `callback` (light or full thunk).
bool RunHooked(uintptr_t callback, bool light, Rs7MidRun::Regs* out) {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!page) {
        return false;
    }

    const uint8_t original[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};

    std::memcpy(page, original, sizeof original);
    const size_t n = light ? Rs7Mid_BuildLight(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), callback)
                           : Rs7Mid_Build(page + 64, 256, page, uintptr_t(page), 5, uintptr_t(page + 64), callback);

    if (n) {
        *out = Rs7MidRun::Run(uintptr_t(page + 64), 0x202);
    }

    VirtualFree(page, 0, MEM_RELEASE);

    return n > 0;
}
}  // namespace

TEST_NO_ASAN(midhook_guard_skips_a_faulting_callback_with_the_registers_unchanged) {
    Rs7Mid_SetFaultLog(&Record);

    for (const bool light : {true, false}) {
        const long before = Rs7Mid_FaultCount();
        Rs7MidRun::Regs out{};

        g_loggedCallback = 0;
        CHECK(RunHooked(uintptr_t(&EditThenFault), light, &out));
        CHECK_EQ(out.eax, 0x1111u);  // Run's inputs: the callback's edits were undone
        CHECK_EQ(out.ecx, 5u);
        CHECK_EQ(out.esi, 0x5555u);
        CHECK_EQ(out.flags & 0x8d5u, 0x202u & 0x8d5u);
        CHECK_EQ(out.espDelta, 0u);
        CHECK_EQ(Rs7Mid_FaultCount(), before + 1);
        CHECK(g_loggedCallback == uintptr_t(&EditThenFault));
        CHECK_EQ(g_loggedCode, static_cast<unsigned long>(EXCEPTION_ACCESS_VIOLATION));
    }

    Rs7Mid_SetFaultLog(nullptr);
}

TEST_NO_ASAN(midhook_guard_keeps_a_normal_callbacks_edits) {
    const long before = Rs7Mid_FaultCount();
    Rs7MidRun::Regs out{};

    CHECK(RunHooked(uintptr_t(&EditOnly), true, &out));
    CHECK_EQ(out.eax, 0x7777u);
    CHECK_EQ(Rs7Mid_FaultCount(), before);
}

namespace {
// Sse mode: asks the thunk to load a value, then faults.
void __cdecl LoadThenFault(Rs7Mid::Context& c) {
    (void)Rs7Mid::LoadSt0(c, 2.5f);
    c.eax += *g_null;
}

// Full mode: writes over the start of the FXSAVE image (as the St0 edits do), then faults.
void __cdecl EditImageThenFault(Rs7Mid::Context& c) {
    std::memset(reinterpret_cast<void*>(c.fpu), 0xAB, 160);
    c.eax += *g_null;
}
}  // namespace

// A skipped callback leaves nothing outside the Context either: the replayed displaced `fld` is the only load.
TEST_NO_ASAN(midhook_guard_drops_the_sse_load_of_a_faulting_callback) {
    Rs7Mid::SseResult result{};
    Rs7Mid::Context c{};

    c.fpu = uint32_t(reinterpret_cast<uintptr_t>(&result)) | 1;
    Rs7Mid_Guard(&LoadThenFault, &c);
    CHECK_EQ(result.load, 0u);
    CHECK_EQ(c.fpu, uint32_t(reinterpret_cast<uintptr_t>(&result)) | 1);
}

TEST_NO_ASAN(midhook_guard_restores_the_fpu_image_of_a_faulting_callback) {
    alignas(16) uint8_t image[512];
    uint8_t before[512];

    for (size_t i = 0; i < sizeof image; ++i) {
        image[i] = uint8_t(i * 7);
    }

    std::memcpy(before, image, sizeof image);
    Rs7Mid::Context c{};

    c.fpu = uint32_t(reinterpret_cast<uintptr_t>(image));
    Rs7Mid_Guard(&EditImageThenFault, &c);
    CHECK(std::memcmp(image, before, sizeof image) == 0);
}
