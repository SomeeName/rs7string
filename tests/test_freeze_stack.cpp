#include "platform/freeze_stack.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>

namespace {
struct Stack {
    static constexpr size_t kBytes = 128 * 1024;
    uintptr_t* words = rs7test::Must(
        static_cast<uintptr_t*>(VirtualAlloc(nullptr, kBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)), "stack");
    uintptr_t lo = reinterpret_cast<uintptr_t>(words), hi = lo + kBytes;

    ~Stack() { VirtualFree(words, 0, MEM_RELEASE); }
};
}  // namespace

TEST(frozen_stack_refuses_esp_outside_known_bounds) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};

    for (const uintptr_t esp : {stack.lo - sizeof(uintptr_t), stack.hi, stack.hi + sizeof(uintptr_t)}) {
        const Rs7FrozenContext context{0, esp, stack.lo, stack.hi};

        CHECK(context.CallsRange(range));
    }
}

TEST(frozen_stack_stops_at_stack_base_within_a_readable_region) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};
    const uintptr_t base = stack.hi - sizeof(uintptr_t);
    const Rs7FrozenContext context{0, stack.lo, stack.lo, base};

    stack.words[Stack::kBytes / sizeof(uintptr_t) - 1] = 0x1234;
    CHECK(!context.CallsRange(range));
}

TEST(frozen_stack_refuses_deep_guard_without_consuming_it) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};
    const Rs7FrozenContext context{0, stack.lo, stack.lo, stack.hi};
    void* guard = reinterpret_cast<void*>(stack.hi - 4096);
    DWORD old = 0;

    CHECK(VirtualProtect(guard, 4096, PAGE_READWRITE | PAGE_GUARD, &old));
    CHECK(context.CallsRange(range));
    MEMORY_BASIC_INFORMATION memory{};

    CHECK(VirtualQuery(guard, &memory, sizeof memory) != 0);
    CHECK((memory.Protect & PAGE_GUARD) != 0);
}

TEST(frozen_stack_reports_return_address_and_thread_details) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};
    const Rs7FrozenContext context{0x9000, stack.lo, stack.lo, stack.hi};
    Rs7FrozenRefusal refusal;

    stack.words[18000] = 0x1234;
    CHECK(context.CallsRange(range, &refusal, 42));
    CHECK(refusal.reason == Rs7FrozenReason::ReturnAddress);
    CHECK(refusal.tid == 42);
    CHECK(refusal.eip == 0x9000);
    CHECK(refusal.esp == stack.lo);
    CHECK(refusal.stackLimit == stack.lo);
    CHECK(refusal.stackBase == stack.hi);
    CHECK(refusal.address == stack.lo + 18000 * sizeof(uintptr_t));
    CHECK(refusal.value == 0x1234);
    CHECK(refusal.range.lo == range.lo && refusal.range.hi == range.hi);
}

TEST(frozen_stack_reports_entry_even_when_capture_failed) {
    const Rs7Range range{0x1234, 0x1238};
    const auto context = Rs7FrozenContext::Capture(GetCurrentThread(), 0x1234, 0, 0);
    Rs7FrozenRefusal refusal;

    CHECK(context.CallsRange(range, &refusal));
    CHECK(refusal.reason == Rs7FrozenReason::Eip);
    CHECK(refusal.address == 0x1234);
    CHECK(refusal.value == 0x1234);
}

TEST(frozen_stack_reports_missing_fs_without_losing_context) {
    const Rs7Range range{0x1234, 0x1238};
    const auto context = Rs7FrozenContext::Capture(GetCurrentThread(), 0x9000, 0x8000, 0);
    Rs7FrozenRefusal refusal;

    CHECK(context.CallsRange(range, &refusal, 51));
    CHECK(refusal.reason == Rs7FrozenReason::NoFs);
    CHECK(refusal.tid == 51 && refusal.eip == 0x9000 && refusal.esp == 0x8000);
}

TEST(frozen_stack_reports_bounds_alignment_and_guard_separately) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};
    Rs7FrozenContext context{0, stack.lo - 4, stack.lo, stack.hi};
    Rs7FrozenRefusal refusal;

    CHECK(context.CallsRange(range, &refusal));
    CHECK(refusal.reason == Rs7FrozenReason::EspOutside);
    context.esp = stack.lo + 1;
    CHECK(context.CallsRange(range, &refusal));
    CHECK(refusal.reason == Rs7FrozenReason::Unaligned);
    context.esp = stack.lo;
    DWORD old = 0;

    CHECK(VirtualProtect(reinterpret_cast<void*>(stack.hi - 4096), 4096, PAGE_READWRITE | PAGE_GUARD, &old));
    CHECK(context.CallsRange(range, &refusal));
    CHECK(refusal.reason == Rs7FrozenReason::GuardPage);
    CHECK(refusal.address == stack.hi - 4096);
    CHECK((refusal.protect & PAGE_GUARD) != 0);
    MEMORY_BASIC_INFORMATION memory{};

    CHECK(VirtualQuery(reinterpret_cast<void*>(refusal.address), &memory, sizeof memory) != 0);
    CHECK((memory.Protect & PAGE_GUARD) != 0);
}

TEST(frozen_stack_reports_uncommitted_page_before_base) {
    Stack stack;
    const Rs7Range range{0x1234, 0x1238};
    const Rs7FrozenContext context{0, stack.lo, stack.lo, stack.hi};

    // Decommit only (C6250): the test needs an uncommitted page inside a still-reserved stack.
#pragma warning(suppress : 6250)
    CHECK(VirtualFree(reinterpret_cast<void*>(stack.hi - 4096), 4096, MEM_DECOMMIT));
    Rs7FrozenRefusal refusal;

    CHECK(context.CallsRange(range, &refusal));
    CHECK(refusal.reason == Rs7FrozenReason::Unreadable);
    CHECK(refusal.address == stack.hi - 4096);
    CHECK(refusal.state == MEM_RESERVE);
}
