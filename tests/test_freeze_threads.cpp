#include "platform/freeze.h"
#include "test.h"
#include <windows.h>

namespace {
struct Worker {
    volatile LONG stop = 0, count = 0, childCount = 0, spawn = 0;
    HANDLE thread = nullptr, child = nullptr;
    DWORD tid = 0;

    static DWORD WINAPI Child(void* p) {
        auto& w = *static_cast<Worker*>(p);

        while (!InterlockedCompareExchange(&w.stop, 0, 0)) {
            InterlockedIncrement(&w.childCount);
            Sleep(0);
        }

        return 0;
    }

    static DWORD WINAPI Run(void* p) {
        auto& w = *static_cast<Worker*>(p);

        while (!InterlockedCompareExchange(&w.stop, 0, 0)) {
            if (InterlockedExchange(&w.spawn, 0)) {
                w.child = CreateThread(nullptr, 0, Child, p, 0, nullptr);
            }
            InterlockedIncrement(&w.count);
            Sleep(0);
        }

        return 0;
    }

    Worker() { thread = rs7test::Must(CreateThread(nullptr, 0, Run, this, 0, &tid), "CreateThread"); }

    ~Worker() {
        InterlockedExchange(&stop, 1);
        CHECK_EQ(WaitForSingleObject(thread, 5000), WAIT_OBJECT_0);
        CloseHandle(thread);

        if (child) {
            CHECK_EQ(WaitForSingleObject(child, 5000), WAIT_OBJECT_0);
            CloseHandle(child);
        }
    }

    bool Moves() {
        const LONG before = InterlockedCompareExchange(&count, 0, 0);
        const ULONGLONG end = GetTickCount64() + 5000;

        while (GetTickCount64() < end) {
            if (InterlockedCompareExchange(&count, 0, 0) != before) {
                return true;
            }

            Sleep(1);
        }

        return false;
    }
};

Worker* g_target = nullptr;
bool g_spawnChild = false;
volatile LONG g_contextCalls = 0, g_suspendCalls = 0;

struct Hooks {
    explicit Hooks(Worker& w) {
        g_target = &w;
        g_spawnChild = false;
        InterlockedExchange(&g_contextCalls, 0);
        InterlockedExchange(&g_suspendCalls, 0);
        Rs7_FreezeSetTiming(150, 2000);
    }

    ~Hooks() {
        const ULONGLONG end = GetTickCount64() + 6000;

        while (Rs7_FreezeReaderBusy() && GetTickCount64() < end) {
            Sleep(1);
        }

        CHECK(!Rs7_FreezeReaderBusy());
        if (Rs7_FreezeReaderBusy()) {
            std::abort();  // a still-running reader must not outlive its fixture
        }

        Rs7_FreezeSetTestHooks(nullptr, nullptr);
        Rs7_FreezeSetTiming(0, 0);
        g_target = nullptr;
    }
};

void Count(void* p) {
    ++*static_cast<int*>(p);
}

int FailContext(void* h, unsigned long tid, void* c) {
    return tid == g_target->tid ? FALSE : GetThreadContext(static_cast<HANDLE>(h), static_cast<CONTEXT*>(c));
}

unsigned long FailSuspend(void* h, unsigned long tid) {
    return tid == g_target->tid ? DWORD(-1) : SuspendThread(static_cast<HANDLE>(h));
}

// The first context read only completes after the coordinator releases the target. The target can then
// create a new runnable child, which was absent from the freeze's original enumeration.
int ReleaseContext(void* h, unsigned long tid, void* c) {
    if (tid == g_target->tid && InterlockedIncrement(&g_contextCalls) == 1) {
        // Acknowledge the asynchronous initial suspension before asking the target to create anything.
        if (!GetThreadContext(static_cast<HANDLE>(h), static_cast<CONTEXT*>(c))) {
            return FALSE;
        }

        const LONG before = InterlockedCompareExchange(&g_target->count, 0, 0);
        const ULONGLONG end = GetTickCount64() + 5000;

        if (g_spawnChild) {
            InterlockedExchange(&g_target->spawn, 1);
        }

        while (GetTickCount64() < end) {
            const bool ready = g_spawnChild ? InterlockedCompareExchange(&g_target->childCount, 0, 0) != 0
                                            : InterlockedCompareExchange(&g_target->count, 0, 0) != before;
            if (ready || Rs7_FreezeAbortedForTests()) {
                break;
            }

            Sleep(1);
        }
    }

    return GetThreadContext(static_cast<HANDLE>(h), static_cast<CONTEXT*>(c));
}

unsigned long FailResuspend(void* h, unsigned long tid) {
    if (tid == g_target->tid && InterlockedIncrement(&g_suspendCalls) > 1) {
        return DWORD(-1);
    }

    return SuspendThread(static_cast<HANDLE>(h));
}

struct ChildProbe {
    Worker* worker;
    LONG before = 0, after = 0;
};

void WatchChild(void* p) {
    auto& probe = *static_cast<ChildProbe*>(p);

    probe.before = InterlockedCompareExchange(&probe.worker->childCount, 0, 0);
    const ULONGLONG end = GetTickCount64() + 20;

    while (GetTickCount64() < end) {
    }
    probe.after = InterlockedCompareExchange(&probe.worker->childCount, 0, 0);
}
}  // namespace

TIMING_TEST(freeze_refuses_a_live_thread_with_an_unreadable_context) {
    Worker w;
    Hooks hooks(w);

    CHECK(w.Moves());
    Rs7_FreezeSetTestHooks(nullptr, FailContext);
    int calls = 0;
    Rs7FreezeStats st;

    CHECK(!Rs7_RunFrozen(nullptr, 0, Count, &calls, &st, 0));
    CHECK_EQ(calls, 0);
    CHECK(w.Moves());
}

TIMING_TEST(freeze_refuses_a_live_thread_that_cannot_be_suspended) {
    Worker w;
    Hooks hooks(w);

    CHECK(w.Moves());
    Rs7_FreezeSetTestHooks(FailSuspend, nullptr);
    int calls = 0;
    Rs7FreezeStats st;

    CHECK(!Rs7_RunFrozen(nullptr, 0, Count, &calls, &st, 0));
    CHECK_EQ(calls, 0);
    CHECK(w.Moves());
}

TIMING_TEST(freeze_refuses_a_live_thread_that_cannot_be_resuspended) {
    Worker w;
    Hooks hooks(w);

    CHECK(w.Moves());
    Rs7_FreezeSetTestHooks(FailResuspend, ReleaseContext);
    int calls = 0;
    Rs7FreezeStats st;

    CHECK(!Rs7_RunFrozen(nullptr, 0, Count, &calls, &st, 2000));
    CHECK_EQ(calls, 0);
    CHECK_EQ(st.released, 1u);
    CHECK(w.Moves());
}

TIMING_TEST(freeze_holds_a_child_created_by_a_temporarily_released_target) {
    Worker w;
    Hooks hooks(w);

    CHECK(w.Moves());
    g_spawnChild = true;
    Rs7_FreezeSetTestHooks(nullptr, ReleaseContext);
    ChildProbe probe{&w};
    Rs7FreezeStats st;

    CHECK(Rs7_RunFrozen(nullptr, 0, WatchChild, &probe, &st, 2000));
    CHECK(st.released >= 1u);
    CHECK_EQ(st.stalls, 0u);  // exercise a temporary release, without a full abort/retry hiding the child
    CHECK(probe.before > 0);
    CHECK_EQ(probe.before, probe.after);
    CHECK(w.Moves());
}
