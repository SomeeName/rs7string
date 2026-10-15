#include "core/diagrams.h"
#include "core/freeze.h"
#include "core/patch.h"
#include "hooks/isolate.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
volatile LONG g_spin = 0, g_stop = 0, g_progressRequested = 0;

DWORD WINAPI Counter(LPVOID progress) {
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
        const bool notify = InterlockedExchange(&g_progressRequested, 0) != 0;

        InterlockedIncrement(&g_spin);
        if (notify) {
            SetEvent(static_cast<HANDLE>(progress));
        }
    }

    return 0;
}

double NowMs() {
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);

    return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}

struct Probe {
    LONG before, after;
};

void Watch(void* p) {  // inside the freeze: no allocation, no logging
    Probe& pr = *static_cast<Probe*>(p);

    pr.before = g_spin;
    const double t = NowMs();

    while (NowMs() - t < 20) {
    }
    pr.after = g_spin;
}

// A thread parked in executable memory: 90 90 EB FE (nop, nop, jmp $) - its EIP stays at code + 2.
struct Parked {
    uint8_t* code = nullptr;
    HANDLE h = nullptr;
    DWORD tid = 0;
    bool ready = false;

    Parked() {
        code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) {
            return;
        }

        const uint8_t loop[] = {0x90, 0x90, 0xEB, 0xFE};

        std::memcpy(code, loop, sizeof loop);
        FlushInstructionCache(GetCurrentProcess(), code, sizeof loop);
        h = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(code), nullptr, 0, &tid);
        if (!h) {
            return;
        }

        // Thread creation does not mean its entry point has run. Observe the actual loop EIP,
        // always resuming before checking/reporting, so startup under load cannot fake readiness.
        const ULONGLONG deadline = GetTickCount64() + 5000;

        do {
            if (SuspendThread(h) == DWORD(-1)) {
                return;
            }

            CONTEXT context{};

            context.ContextFlags = CONTEXT_CONTROL;
            const bool gotContext = GetThreadContext(h, &context) != FALSE;
            const DWORD resumed = ResumeThread(h);
            if (!gotContext || resumed == DWORD(-1)) {
                return;
            }

            ready = context.Eip == reinterpret_cast<uintptr_t>(code) + 2;
            if (ready) {
                return;
            }

            Sleep(1);  // bounded condition wait, not a readiness assumption
        } while (GetTickCount64() < deadline);
    }

    ~Parked() {
        if (h) {
            TerminateThread(h, 0);  // test only: the loop never returns
            WaitForSingleObject(h, 2000);
            CloseHandle(h);
        }

        if (code) {
            VirtualFree(code, 0, MEM_RELEASE);
        }
    }
};

void Nothing(void*) {}
}  // namespace

TEST(freeze_lists_this_process_threads) {
    unsigned long ids[1024];
    const size_t n = Rs7_FreezeListThreads(ids, 1024);
    bool self = false;

    for (size_t i = 0; i < n; ++i) {
        self = self || ids[i] == GetCurrentThreadId();
    }

    CHECK(n >= 1);
    CHECK(self);
    Parked p;

    CHECK(p.ready);
    if (!p.ready) {
        return;
    }

    const size_t m = Rs7_FreezeListThreads(ids, 1024);
    bool other = false;

    for (size_t i = 0; i < m; ++i) {
        other = other || ids[i] == p.tid;
    }

    CHECK(other);
}

TEST(freeze_stops_every_other_thread_while_fn_runs) {
    InterlockedExchange(&g_stop, 0);
    InterlockedExchange(&g_spin, 0);
    InterlockedExchange(&g_progressRequested, 1);
    const HANDLE progress = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    CHECK(progress != nullptr);
    if (!progress) {
        return;
    }

    const HANDLE t = CreateThread(nullptr, 0, Counter, progress, 0, nullptr);

    CHECK(t != nullptr);

    if (!t) {
        CloseHandle(progress);
        return;
    }

    const auto stopCounter = [&] {
        InterlockedExchange(&g_stop, 1);
        const DWORD stopped = WaitForSingleObject(t, 5000);

        CHECK_EQ(stopped, WAIT_OBJECT_0);
        CloseHandle(t);
        // On a failed join the worker may still be about to acknowledge progress. Keep its event
        // alive until process exit rather than letting it signal a closed/reused handle.
        if (stopped == WAIT_OBJECT_0) {
            CloseHandle(progress);
        }
    };
    const DWORD started = WaitForSingleObject(progress, 5000);

    CHECK_EQ(started, WAIT_OBJECT_0);

    if (started != WAIT_OBJECT_0) {
        stopCounter();
        return;
    }

    Probe pr{};
    Rs7FreezeStats st;

    CHECK(Rs7_RunFrozen(nullptr, 0, &Watch, &pr, &st));
    CHECK_EQ(pr.before, pr.after);  // the counter did not move for 20 ms
    CHECK(st.threads >= 1);
    CHECK_EQ(st.retries, 0u);
    CHECK(st.frozenMs >= 15.0);
    // Ask for a new increment after the freeze, then wait for its acknowledgement. A fixed sleep
    // can expire before the resumed counter gets scheduled, even when ResumeThread worked.
    InterlockedExchange(&g_progressRequested, 1);
    CHECK_EQ(WaitForSingleObject(progress, 5000), WAIT_OBJECT_0);
    stopCounter();
}

TEST(freeze_times_out_without_calling_fn_while_a_thread_stands_inside_a_range) {
    Parked p;

    CHECK(p.ready);
    if (!p.ready) {
        return;
    }

    const uintptr_t c = reinterpret_cast<uintptr_t>(p.code);
    Rs7Range inside[2] = {{c + 0x100, c + 0x108}, {c, c + 4}};  // the loop at c + 2 is interior to range 1
    int calls = 0;
    Rs7FreezeStats st;

    // A zero deadline makes the first conflict exhaust the budget deterministically. With a 50 ms
    // budget, descheduling during the first attempt can legitimately leave only one conflict.
    CHECK(!Rs7_RunFrozen(inside, 2, [](void* n) { ++*static_cast<int*>(n); }, &calls, &st, 0));
    CHECK_EQ(calls, 0);
    CHECK(st.timedOut);
    CHECK_EQ(st.retries, 1u);
    CHECK_EQ(st.busyTid, p.tid);
    CHECK_EQ(st.busyEip, c + 2);
    CHECK_EQ(st.busyRange, size_t(1));
    char line[256];

    Rs7_FreezeDescribe(st, line, sizeof line);
    CHECK(std::strstr(line, "DEADLINE") != nullptr);
    // A thread exactly at the start of a range has not fetched it yet: not a conflict.
    Rs7Range atStart{c + 2, c + 4};

    CHECK(Rs7_RunFrozen(&atStart, 1, [](void* n) { ++*static_cast<int*>(n); }, &calls, &st, 50));
    CHECK_EQ(calls, 1);
    CHECK(!st.timedOut);
}

TEST(freeze_patch_round_writes_and_rolls_back_under_one_freeze) {
    // The isolate policy with ranges: a failing write restores the round inside the same freeze.
    static uint8_t buf[3][8];

    std::memset(buf, 0x11, sizeof buf);

    struct Ctx {
        int applied = 0, restored = 0;
        bool failSecond = true;
        std::vector<std::string> log;
    } x;

    Rs7PatchOps ops;

    ops.count = 3;
    ops.feature = [](size_t i) {
        return i == 0 ? Rs7Feature::Core : Rs7Feature::Diagrams;
    };
    ops.name = [](size_t i) {
        static const char* n[3] = {"A", "B", "C"};

        return n[i];
    };
    ops.should = [&](size_t i) {
        return i == 0 || !Rs7_FeatureDisabled(Rs7Feature::Diagrams);
    };
    ops.prepare = [](size_t) {
        return true;
    };
    ops.apply = [&](size_t i) {
        if (i == 1 && x.failSecond) {
            return false;
        }

        std::memset(buf[i], 0x22, 8);
        ++x.applied;

        return true;
    };
    ops.restore = [&](size_t i) {
        std::memset(buf[i], 0x11, 8);
        ++x.restored;

        return true;
    };
    ops.range = [](size_t i) {
        return Rs7Range{reinterpret_cast<uintptr_t>(buf[i]), reinterpret_cast<uintptr_t>(buf[i]) + 8};
    };
    ops.log = [&](const std::string& m) {
        x.log.push_back(m);
    };
    const auto savedDg = g_rs7_diagrams;

    Rs7_ResetFeatureFailures();
    std::vector<size_t> written;

    CHECK(Rs7_PatchIsolated(ops, written) == Rs7PatchResult::Ok);
    CHECK_EQ(written.size(), size_t(1));  // B's group disabled, round 2 wrote A only
    CHECK_EQ(buf[0][0], 0x22);
    CHECK_EQ(buf[1][0], 0x11);
    CHECK_EQ(x.restored, 1);  // A undone in round 1
    int freezes = 0;

    for (const auto& l : x.log) {
        freezes += l.rfind("freeze: ", 0) == 0;
    }

    CHECK_EQ(freezes, 2);  // one freeze per round, logged after it
    Rs7_ResetFeatureFailures();
    g_rs7_diagrams = savedDg;
}

// ---- bounded thread control (startup hang, smoke frame-2: GetThreadContext blocked inside WoW64) ----------
// The hooks below stand in for SuspendThread / GetThreadContext on the freeze's reader thread. WowLike
// reproduces the WoW64 behaviour that hung the game: the call only returns once the target thread has run
// again, which it cannot while the freeze holds it suspended.
namespace {
struct Spinner {
    volatile LONG count = 0, stop = 0;
    HANDLE h = nullptr;
    DWORD tid = 0;

    static DWORD WINAPI Run(LPVOID p) {
        auto* s = static_cast<Spinner*>(p);

        while (!InterlockedCompareExchange(&s->stop, 0, 0)) {
            InterlockedIncrement(&s->count);
            Sleep(0);
        }

        return 0;
    }

    Spinner() { h = CreateThread(nullptr, 0, Run, this, 0, &tid); }

    ~Spinner() {
        InterlockedExchange(&stop, 1);

        if (h) {
            WaitForSingleObject(h, 5000);
            CloseHandle(h);
        }
    }

    bool Moves() const {  // makes progress within 2 s
        const LONG c0 = count;

        for (int i = 0; i < 2000; ++i) {
            if (count != c0) {
                return true;
            }

            Sleep(1);
        }

        return false;
    }

    DWORD SuspendCount() const {  // our own suspend/resume pair: the previous count
        const DWORD prev = SuspendThread(h);

        ResumeThread(h);

        return prev;
    }
};

Spinner* g_victim = nullptr;
volatile LONG g_hookStalls = 0;  // how many more calls on the victim should stall
HANDLE g_releaseWedge = nullptr;
unsigned g_suspendStallMs = 0;

int WowLikeContext(void* h, unsigned long tid, void* c) {
    if (g_victim && tid == g_victim->tid && InterlockedCompareExchange(&g_hookStalls, 0, 0) > 0) {
        InterlockedDecrement(&g_hookStalls);
        const LONG c0 = g_victim->count;

        for (int i = 0; i < 10000 && g_victim->count == c0; ++i) {
            Sleep(1);  // until the victim has run
        }
    }

    return GetThreadContext(static_cast<HANDLE>(h), static_cast<CONTEXT*>(c));
}

int WedgedContext(void* h, unsigned long tid, void* c) {
    if (g_victim && tid == g_victim->tid) {
        WaitForSingleObject(g_releaseWedge, 10000);
    }

    return GetThreadContext(static_cast<HANDLE>(h), static_cast<CONTEXT*>(c));
}

unsigned long SlowSuspend(void* h, unsigned long tid) {
    if (g_victim && tid == g_victim->tid && InterlockedCompareExchange(&g_hookStalls, 0, 0) > 0) {
        InterlockedDecrement(&g_hookStalls);
        Sleep(g_suspendStallMs);
    }

    return SuspendThread(static_cast<HANDLE>(h));
}

void Count(void* n) {
    ++*static_cast<int*>(n);
}

void ResetHooks() {
    Rs7_FreezeSetTestHooks(nullptr, nullptr);
    Rs7_FreezeSetTiming(0, 0);
    g_victim = nullptr;
    InterlockedExchange(&g_hookStalls, 0);
}
}  // namespace

TEST(freeze_context_stall_resumes_everything_and_retries) {
    Spinner v;

    CHECK(v.h != nullptr && v.Moves());
    if (!v.h) {
        return;
    }

    g_victim = &v;
    InterlockedExchange(&g_hookStalls, 1);  // the first context read stalls, the retry does not
    Rs7_FreezeSetTestHooks(nullptr, &WowLikeContext);
    Rs7_FreezeSetTiming(50, 2000);
    int calls = 0;
    Rs7FreezeStats st;
    const double t = NowMs();

    CHECK(Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st, 2000));
    const double took = NowMs() - t;

    ResetHooks();
    CHECK_EQ(calls, 1);
    CHECK_EQ(st.stalls, 1u);
    CHECK_EQ(st.nEvents, 1u);
    CHECK_EQ(st.events[0].op, 'C');
    CHECK_EQ(st.events[0].tid, static_cast<unsigned long>(v.tid));
    CHECK(st.events[0].releaseMs >= 0.0f);
    CHECK(took < 1500.0);
    CHECK_EQ(v.SuspendCount(), DWORD(0));  // balanced: nothing left suspended
    CHECK(v.Moves());
    char line[512];

    Rs7_FreezeDescribe(st, line, sizeof line);
    CHECK(std::strstr(line, "1 stalls") != nullptr);
    CHECK(std::strstr(line, "stall 1: GetThreadContext") != nullptr);
}

TEST(freeze_persistent_context_stall_gives_up_at_the_deadline_without_writing) {
    Spinner v;

    CHECK(v.h != nullptr && v.Moves());
    if (!v.h) {
        return;
    }

    g_victim = &v;
    InterlockedExchange(&g_hookStalls, 1000);  // every attempt stalls
    Rs7_FreezeSetTestHooks(nullptr, &WowLikeContext);
    Rs7_FreezeSetTiming(50, 2000);
    int calls = 0;
    Rs7FreezeStats st;
    const double t = NowMs();

    CHECK(!Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st, 300));
    const double took = NowMs() - t;

    ResetHooks();
    CHECK_EQ(calls, 0);
    CHECK(st.timedOut);
    CHECK(st.lastWasStall);
    CHECK(!st.wedged);
    CHECK(st.stalls >= 2u);
    CHECK(took < 300.0 + 2000.0 + 500.0);  // deadline + one stall/grace, never unbounded
    CHECK_EQ(v.SuspendCount(), DWORD(0));
    CHECK(v.Moves());
    char line[1024];

    Rs7_FreezeDescribe(st, line, sizeof line);
    CHECK(std::strstr(line, "DEADLINE") != nullptr);
    CHECK(std::strstr(line, "kept stalling") != nullptr);
}

TEST(freeze_wedged_reader_releases_threads_and_refuses_until_it_returns) {
    Spinner v;

    CHECK(v.h != nullptr && v.Moves());
    if (!v.h) {
        return;
    }

    g_releaseWedge = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(g_releaseWedge != nullptr);
    if (!g_releaseWedge) {
        return;
    }

    g_victim = &v;
    Rs7_FreezeSetTestHooks(nullptr, &WedgedContext);
    Rs7_FreezeSetTiming(50, 200);
    int calls = 0;
    Rs7FreezeStats st;

    CHECK(!Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st, 2000));
    CHECK_EQ(calls, 0);
    CHECK(st.wedged);
    CHECK_EQ(st.stalls, 1u);
    CHECK(st.events[0].releaseMs < 0.0f);
    // The reader is still blocked, yet no thread stays suspended.
    CHECK(Rs7_FreezeReaderBusy());
    CHECK(v.Moves());
    char line[512];

    Rs7_FreezeDescribe(st, line, sizeof line);
    CHECK(std::strstr(line, "WEDGED") != nullptr);
    CHECK(std::strstr(line, "reader NOT back") != nullptr);
    // While it is blocked every freeze is refused at once.
    Rs7FreezeStats st2;
    const double t = NowMs();

    CHECK(!Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st2, 2000));
    CHECK(NowMs() - t < 100.0);
    CHECK(st2.readerBusy);
    CHECK_EQ(calls, 0);
    Rs7_FreezeDescribe(st2, line, sizeof line);
    CHECK(std::strstr(line, "REFUSED") != nullptr);
    // Once the call returns the reader cleans up and freezes work again.
    SetEvent(g_releaseWedge);

    for (int i = 0; i < 5000 && Rs7_FreezeReaderBusy(); ++i) {
        Sleep(1);
    }

    CHECK(!Rs7_FreezeReaderBusy());
    ResetHooks();
    Rs7FreezeStats st3;

    CHECK(Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st3, 2000));
    CHECK_EQ(calls, 1);
    CHECK_EQ(st3.stalls, 0u);
    CHECK_EQ(v.SuspendCount(), DWORD(0));
    CloseHandle(g_releaseWedge);
    g_releaseWedge = nullptr;
}

TEST(freeze_suspend_stall_resumes_what_the_reader_suspends_late) {
    Spinner v;

    CHECK(v.h != nullptr && v.Moves());
    if (!v.h) {
        return;
    }

    g_victim = &v;
    g_suspendStallMs = 300;
    InterlockedExchange(&g_hookStalls, 1);  // first SuspendThread on the victim takes 300 ms
    Rs7_FreezeSetTestHooks(&SlowSuspend, nullptr);
    Rs7_FreezeSetTiming(50, 2000);
    int calls = 0;
    Rs7FreezeStats st;

    CHECK(Rs7_RunFrozen(nullptr, 0, &Count, &calls, &st, 2000));
    ResetHooks();
    CHECK_EQ(calls, 1);
    CHECK_EQ(st.stalls, 1u);
    CHECK_EQ(st.events[0].op, 'S');
    CHECK_EQ(st.events[0].tid, static_cast<unsigned long>(v.tid));
    CHECK(st.events[0].releaseMs >= 0.0f);
    CHECK_EQ(v.SuspendCount(), DWORD(0));  // the late suspend was undone by the reader itself
    CHECK(v.Moves());
}
