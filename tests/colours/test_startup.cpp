#include "features/colours/colours.h"
#include "features/colours/state.h"
#include "hook_rules.h"
#include "platform/freeze.h"
#include "platform/resolve.h"
#include "startup_sites.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstring>
#include <memory>

namespace {
struct Batch {
    static constexpr size_t kAlloc = 1 + (0x518AC4 - 0x5184F6);
    rsxrtest::Restore<uintptr_t> colours{g_rsxr_native_colours};
    rsxrtest::Restore<uintptr_t> managerRef{g_rsxr_colormgr};
    void* manager = nullptr;
    uint8_t bytes[kAlloc + 1] = {};
    const RsxrHook core = Rsxr_Site("batch-core");
    const RsxrSite sites[3] = {
        {"batch-core", "", 0, "00", RsxrKind::Bytes, 1, -1, 0, 0, "01"},
        {"CM5184F6", "", 0, "00", RsxrKind::Bytes, 1, -1, 0, 0, "01"},
        {"CM518AC4", "", 0, "00", RsxrKind::Bytes, 1, -1, 0, 0, "01"},
    };
    RsxrResolveResult resolved{};
    std::vector<const RsxrHook*> hooks;
    std::vector<size_t> written;

    Batch() {
        Rsxr_ResetFeatureFailures();
        g_rsxr_native_colours = 1;
        g_rsxr_colormgr = reinterpret_cast<uintptr_t>(&manager);

        for (size_t i = 0; i < 3; ++i) {
            const size_t offset = i == 2 ? kAlloc : i;

            resolved.sites.push_back({&sites[i], reinterpret_cast<uintptr_t>(&bytes[offset]), 0, {0}});
            hooks.push_back(i == 0 ? &core : Rsxr_FindHook(sites[i].id));
        }
    }

    ~Batch() { Rsxr_ResetFeatureFailures(); }

    RsxrPatchResult Write() { return RsxrStartup_WriteSites(resolved, hooks, ",", written); }
};

DWORD g_targetTid = 0;
uintptr_t g_targetEip = 0;
uintptr_t g_targetEsp = 0;
bool g_unknownStack = false;
bool g_dirtyFs = false;  // real game threads report SegFs with garbage above the 16-bit selector
bool g_exitTarget = false;
HANDLE g_targetThread = nullptr;  // the test's own handle: the freeze's handles cannot terminate

int ConstructorContext(void* thread, unsigned long tid, void* raw) {
    if (g_exitTarget && tid == g_targetTid) {
        // The target is suspended here and cannot exit by itself: end it, then report the failed context read.
#pragma warning(push)
#pragma warning(disable : 6258)
        TerminateThread(g_targetThread, 0);
#pragma warning(pop)
        WaitForSingleObject(g_targetThread, 5000);

        return 0;
    }

    const int got = GetThreadContext(static_cast<HANDLE>(thread), static_cast<CONTEXT*>(raw));

    if (got && tid == g_targetTid) {
        static_cast<CONTEXT*>(raw)->Eip = DWORD(g_targetEip);
        if (g_targetEsp) {
            static_cast<CONTEXT*>(raw)->Esp = DWORD(g_targetEsp);
        }
        if (g_dirtyFs) {
            static_cast<CONTEXT*>(raw)->SegFs |= 0x1A0000;
        }
        if (g_unknownStack) {
            static_cast<CONTEXT*>(raw)->SegFs = 0;
        }
    }

    return got;
}

DWORD WINAPI WaitForRelease(void* event) {
    WaitForSingleObject(static_cast<HANDLE>(event), INFINITE);

    return 0;
}

struct FrozenCallee {
    static constexpr size_t kStackBytes = 128 * 1024;
    uintptr_t* stack = nullptr;
    HANDLE event = rsxrtest::Must(CreateEventW(nullptr, TRUE, FALSE, nullptr), "callee event");
    HANDLE ready = rsxrtest::Must(CreateEventW(nullptr, TRUE, FALSE, nullptr), "callee ready");
    HANDLE thread = rsxrtest::Must(CreateThread(nullptr, 0, Run, this, 0, &g_targetTid), "callee thread");

    // The words are deliberately on this thread's real stack: the scan must walk past 64 KiB of it.
#pragma warning(push)
#pragma warning(disable : 6262)

    static DWORD WINAPI Run(void* raw) {
        auto* self = static_cast<FrozenCallee*>(raw);
        uintptr_t words[kStackBytes / sizeof(uintptr_t)] = {};

        self->stack = words;
        SetEvent(self->ready);
        WaitForSingleObject(self->event, INFINITE);

        return 0;
    }

#pragma warning(pop)

    FrozenCallee() {
        rsxrtest::Must(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0 ? stack : nullptr, "callee stack");
        // A callee outside both creation ranges, with a clean buffer on its real stack.
        g_targetEip = reinterpret_cast<uintptr_t>(&WaitForRelease);
        g_targetEsp = reinterpret_cast<uintptr_t>(stack);
        g_targetThread = thread;
        Rsxr_FreezeSetTestHooks(nullptr, &ConstructorContext);
    }

    ~FrozenCallee() {
        Rsxr_FreezeSetTestHooks(nullptr, nullptr);
        g_targetEsp = 0;
        g_unknownStack = false;
        g_dirtyFs = false;
        g_exitTarget = false;
        SetEvent(event);
        rsxrtest::Join(thread, 5000);
        CloseHandle(event);
        CloseHandle(ready);
    }
};
}  // namespace

TIMING_TEST(colour_startup_rechecks_manager_after_early_check_before_any_write) {
    Batch batch;

    RsxrColour_CheckManager(GetTickCount());
    CHECK_EQ(g_rsxr_native_colours, 1u);
    int stock = 0;

    batch.manager = &stock;
    CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
    CHECK(batch.written.empty());
    CHECK_EQ(batch.bytes[0], 0u);
    CHECK_EQ(batch.bytes[1], 0u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
    CHECK_EQ(g_rsxr_native_colours, 0u);
    CHECK(std::strcmp(rsxrcolour::g_colour.patchReason, "manager-non-null") == 0);
    CHECK(rsxrcolour::g_colour.patchRefusal.address == reinterpret_cast<uintptr_t>(&batch.manager));
    CHECK(rsxrcolour::g_colour.patchRefusal.value == reinterpret_cast<uintptr_t>(&stock));
}

TIMING_TEST(colour_startup_writes_when_manager_is_still_absent) {
    Batch batch;

    RsxrColour_CheckManager(GetTickCount());
    CHECK(batch.Write() == RsxrPatchResult::Ok);
    CHECK_EQ(batch.written.size(), 3u);
    CHECK_EQ(batch.bytes[0], 1u);
    CHECK_EQ(batch.bytes[1], 1u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 1u);
}

TIMING_TEST(colour_startup_refuses_frozen_constructor_and_allocation_eips) {
    Batch batch;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    CHECK(event != nullptr);
    if (!event) {
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, WaitForRelease, event, 0, &g_targetTid);

    CHECK(thread != nullptr);

    if (!thread) {
        CloseHandle(event);
        return;
    }

    RsxrColour_CheckManager(GetTickCount());
    Rsxr_FreezeSetTestHooks(nullptr, &ConstructorContext);

    // The entry points and a point past the patched allocation immediate must all be refused.
    for (uintptr_t eip : {reinterpret_cast<uintptr_t>(&batch.bytes[1]) - (0x5184F6 - 0x517F70),
                          reinterpret_cast<uintptr_t>(&batch.bytes[Batch::kAlloc]) - 4,
                          reinterpret_cast<uintptr_t>(&batch.bytes[Batch::kAlloc]) + 8}) {
        g_rsxr_native_colours = 1;
        g_targetEip = eip;
        CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
        CHECK(batch.written.empty());
        CHECK_EQ(batch.bytes[0], 0u);
        CHECK_EQ(batch.bytes[1], 0u);
        CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
    }

    Rsxr_FreezeSetTestHooks(nullptr, nullptr);
    SetEvent(event);
    rsxrtest::Join(thread, 5000);
    CloseHandle(event);
}

TIMING_TEST(colour_startup_refuses_allocator_return_into_creation_before_any_write) {
    // On the heap, so no stack neighbour of the batch's buffer (the fake creation code) is hit before the planted word.
    const auto owned = std::make_unique<Batch>();
    const auto calleeOwned = std::make_unique<FrozenCallee>();
    Batch& batch = *owned;
    FrozenCallee& callee = *calleeOwned;

    RsxrColour_CheckManager(GetTickCount());
    // Model the allocator's return into creation beyond the former 64 KiB scan budget.
    callee.stack[FrozenCallee::kStackBytes / sizeof(uintptr_t) - 1] =
        reinterpret_cast<uintptr_t>(&batch.bytes[Batch::kAlloc]) + 10;
    CHECK(batch.manager == nullptr);
    CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
    CHECK(batch.written.empty());
    CHECK_EQ(batch.bytes[0], 0u);
    CHECK_EQ(batch.bytes[1], 0u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
    CHECK_EQ(g_rsxr_native_colours, 0u);
    CHECK(rsxrcolour::g_colour.patchRefusal.reason == RsxrFrozenReason::ReturnAddress);
    CHECK(rsxrcolour::g_colour.patchRefusal.tid == g_targetTid);
    CHECK(rsxrcolour::g_colour.patchRefusal.value == reinterpret_cast<uintptr_t>(&batch.bytes[Batch::kAlloc]) + 10);
}

TIMING_TEST(colour_startup_writes_with_clean_callee_stack) {
    // Both on the heap: the scanned "creation code" is this batch's buffer, and a stack object next to it (like the
    // callee, whose thread keeps a pointer to it) would look like a return address into creation.
    const auto owned = std::make_unique<Batch>();
    const auto calleeOwned = std::make_unique<FrozenCallee>();
    Batch& batch = *owned;

    RsxrColour_CheckManager(GetTickCount());
    CHECK(batch.manager == nullptr);
    CHECK(batch.Write() == RsxrPatchResult::Ok);
    CHECK_EQ(batch.written.size(), 3u);
    CHECK_EQ(batch.bytes[0], 1u);
    CHECK_EQ(batch.bytes[1], 1u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 1u);
}

TIMING_TEST(colour_startup_refuses_callee_return_into_constructor_before_any_write) {
    Batch batch;
    FrozenCallee callee;

    callee.stack[8] = reinterpret_cast<uintptr_t>(&batch.bytes[1]) - (0x5184F6 - 0x517F90);
    CHECK(batch.manager == nullptr);
    CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
    CHECK(batch.written.empty());
    CHECK_EQ(batch.bytes[0], 0u);
    CHECK_EQ(batch.bytes[1], 0u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
}

TIMING_TEST(colour_startup_refuses_unreadable_callee_stack_before_any_write) {
    Batch batch;
    FrozenCallee callee;
    SYSTEM_INFO system{};

    GetSystemInfo(&system);
    // Keep the guard inside the initialized buffer, beyond 64 KiB. Protecting its unaligned start also guards
    // the callee's active call frame: ready signals before SetEvent returns and the wait has been entered.
    const uintptr_t page = system.dwPageSize;
    const uintptr_t address =
        (reinterpret_cast<uintptr_t>(callee.stack) + FrozenCallee::kStackBytes / 2 + page - 1) / page * page;
    void* const guard = reinterpret_cast<void*>(address);
    DWORD old = 0;

    CHECK(VirtualProtect(guard, page, PAGE_READWRITE | PAGE_GUARD, &old));
    CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
    CHECK(batch.written.empty());
    CHECK_EQ(batch.bytes[0], 0u);
    CHECK_EQ(batch.bytes[1], 0u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
    CHECK(rsxrcolour::g_colour.patchRefusal.reason == RsxrFrozenReason::GuardPage);
    CHECK(rsxrcolour::g_colour.patchRefusal.tid == g_targetTid);
    CHECK(rsxrcolour::g_colour.patchRefusal.address == address);
    CHECK((rsxrcolour::g_colour.patchRefusal.protect & PAGE_GUARD) != 0);
    MEMORY_BASIC_INFORMATION memory{};

    CHECK(VirtualQuery(guard, &memory, sizeof memory) != 0);
    CHECK((memory.Protect & PAGE_GUARD) != 0);  // inspection must not consume a stack guard
    CHECK(VirtualProtect(guard, page, old, &old));
}

TIMING_TEST(colour_startup_refuses_unknown_stack_base_before_any_write) {
    Batch batch;
    FrozenCallee callee;

    g_unknownStack = true;
    CHECK(batch.Write() == RsxrPatchResult::PreconditionFailed);
    CHECK(batch.written.empty());
    CHECK_EQ(batch.bytes[0], 0u);
    CHECK_EQ(batch.bytes[1], 0u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 0u);
    CHECK(rsxrcolour::g_colour.patchRefusal.reason == RsxrFrozenReason::NoFs);
    CHECK(rsxrcolour::g_colour.patchRefusal.tid == g_targetTid);
}

TIMING_TEST(colour_startup_ignores_a_confirmed_exited_thread) {
    const auto owned = std::make_unique<Batch>();
    const auto calleeOwned = std::make_unique<FrozenCallee>();
    Batch& batch = *owned;

    // Its context cannot be read, but it has exited: it runs no game code, so it must not refuse the batch.
    g_exitTarget = true;
    RsxrColour_CheckManager(GetTickCount());
    CHECK(batch.Write() == RsxrPatchResult::Ok);
    CHECK_EQ(batch.written.size(), 3u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 1u);
}

TIMING_TEST(colour_startup_uses_only_the_low_16_bits_of_segfs) {
    const auto owned = std::make_unique<Batch>();
    const auto calleeOwned = std::make_unique<FrozenCallee>();
    Batch& batch = *owned;

    g_dirtyFs = true;
    RsxrColour_CheckManager(GetTickCount());
    CHECK(batch.Write() == RsxrPatchResult::Ok);
    CHECK_EQ(batch.written.size(), 3u);
    CHECK_EQ(batch.bytes[Batch::kAlloc], 1u);
}
