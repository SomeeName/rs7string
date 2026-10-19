#include "platform/freeze.h"
#include "platform/protect.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <winternl.h>

namespace {
constexpr size_t kMaxThreads = 1024;
constexpr LONG kInfoLengthMismatch = LONG(0xC0000004);

using NtQsi = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

SRWLOCK g_lock = SRWLOCK_INIT;  // one freeze at a time (the tables below are shared)
NtQsi g_qsi = nullptr;
uint8_t* g_buf = nullptr;  // touched by the reader during a job only; resized while it is idle
ULONG g_bufSize = 0;

struct Frozen {
    DWORD tid;
    HANDLE h;
};

// The thread table. The reader appends while a job runs; the coordinator (the Rs7_RunFrozen caller) reads it
// when the reader is idle, or under g_table when it aborts a stalled job. g_table is held only for a few
// instructions by those two threads, neither of which is ever suspended by a freeze.
Frozen g_frozen[kMaxThreads];
size_t g_count = 0;
SRWLOCK g_table = SRWLOCK_INIT;

unsigned g_stallMs = 250;   // no progress for this long: resume everything (normal: < 1 ms per call)
unsigned g_graceMs = 2000;  // then wait this long for the reader to come back
Rs7FreezeSuspendFn g_suspendHook = nullptr;
Rs7FreezeContextFn g_contextHook = nullptr;

// ---- the reader thread: all SuspendThread / GetThreadContext calls of a freeze ----
HANDLE g_reader = nullptr, g_go = nullptr, g_done = nullptr;
DWORD g_readerTid = 0;
bool g_readerReady = false;      // the reader has signalled g_done once after starting
volatile LONG g_readerBusy = 0;  // 1 from SetEvent(g_go) until the reader has finished the job
volatile LONG g_abort = 0;       // the coordinator gave up on the job and resumed the table
volatile LONG g_progress = 0;    // +1 per completed thread-control call
volatile LONG g_curTid = 0;      // the thread the reader is working on
volatile LONG g_curOp = 0;       // 'S' / 'C'

enum class Suspend { Ok, Grow, TooMany, Failed };

struct Job {
    DWORD coordinator = 0;
    const Rs7Range* ranges = nullptr;
    size_t n = 0;
};

struct Result {
    Suspend s = Suspend::Ok;
    unsigned skipped = 0;
    ULONG grow = 0;
    bool busy = false, aborted = false;
    DWORD busyTid = 0;
    uintptr_t busyEip = 0;
    size_t busyRange = 0;
};

Job g_job;     // written by the coordinator before g_go
Result g_res;  // written by the reader before g_done

bool EnsureBuffer(ULONG want) {  // reader idle only
    if (g_buf && g_bufSize >= want) {
        return true;
    }

    if (g_buf) {
        VirtualFree(g_buf, 0, MEM_RELEASE);
    }
    g_bufSize = (want + 0xFFFF) & ~ULONG(0xFFFF);
    g_buf = static_cast<uint8_t*>(VirtualAlloc(nullptr, g_bufSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_buf) {
        g_bufSize = 0;
    }

    return g_buf != nullptr;
}

DWORD WINAPI Reader(void*);

bool Init() {  // outside the freeze only, under g_lock
    if (!g_qsi) {
        const HMODULE nt = GetModuleHandleW(L"ntdll.dll");

        g_qsi = nt ? reinterpret_cast<NtQsi>(GetProcAddress(nt, "NtQuerySystemInformation")) : nullptr;
    }

    Rs7_ProtectMethod();  // resolves the protect syscall (file read, heap) now, never inside a freeze
    if (!g_go) {
        g_go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (!g_done) {
        g_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (!g_reader && g_go && g_done) {
        g_reader = CreateThread(nullptr, 64 * 1024, Reader, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &g_readerTid);
    }
    // A new thread first runs the loader's thread attach (loader lock, every DllMain): that took ~0.4 s once
    // during startup (smoke wmi-5) and looked like a stall. Wait for the reader to come up before any job.
    if (g_reader && !g_readerReady) {
        g_readerReady = WaitForSingleObject(g_done, 5000) == WAIT_OBJECT_0;
    }
    if (!g_qsi || !g_reader || !g_readerReady) {
        return false;
    }
    if (InterlockedCompareExchange(&g_readerBusy, 0, 0)) {
        return true;  // buffer belongs to the reader
    }

    return EnsureBuffer(g_bufSize ? g_bufSize : 0x100000);
}

// Calls visit(tid) for every thread of this process. Heap-free. Returns false (needSize set) when the buffer
// is too small.
template <class F>
bool ForEachThread(F visit, ULONG* needSize) {
    ULONG need = 0;
    const LONG s = g_qsi(5 /* SystemProcessInformation */, g_buf, g_bufSize, &need);

    if (s == kInfoLengthMismatch) {  // grow at least 2x: some kernels report need = 0
        const ULONG twice = g_bufSize * 2;

        *needSize = need + 0x10000 > twice ? need + 0x10000 : twice;
        if (*needSize > 0x4000000) {
            *needSize = 0;  // 64 MB: give up (Failed)
        }
        return false;
    }

    if (s < 0) {
        *needSize = 0;
        return false;
    }

    const HANDLE self = reinterpret_cast<HANDLE>(uintptr_t(GetCurrentProcessId()));

    for (const uint8_t* p = g_buf;;) {
        const auto* pi = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(p);

        if (pi->UniqueProcessId == self) {
            const auto* ti = reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(pi + 1);

            for (ULONG i = 0; i < pi->NumberOfThreads; ++i) {
                if (!visit(static_cast<DWORD>(reinterpret_cast<uintptr_t>(ti[i].ClientId.UniqueThread)))) {
                    break;
                }
            }

            return true;
        }

        if (!pi->NextEntryOffset) {
            return true;
        }

        p += pi->NextEntryOffset;
    }
}

void ResumeAll() {  // coordinator, reader idle
    for (size_t i = 0; i < g_count; ++i) {
        ResumeThread(g_frozen[i].h);
        CloseHandle(g_frozen[i].h);
    }

    g_count = 0;
}

bool Known(DWORD tid) {
    for (size_t i = 0; i < g_count; ++i) {
        if (g_frozen[i].tid == tid) {
            return true;
        }
    }

    return false;
}

double Ms(LARGE_INTEGER a, LARGE_INTEGER b) {
    LARGE_INTEGER f;

    QueryPerformanceFrequency(&f);

    return double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
}

bool Aborted() {
    return InterlockedCompareExchange(&g_abort, 0, 0) != 0;
}

void Working(DWORD tid, char op) {
    InterlockedExchange(&g_curTid, LONG(tid));
    InterlockedExchange(&g_curOp, op);
}

// Reader. Suspends every other thread (not the coordinator, not itself); repeats the listing until a pass
// finds no new thread (a thread may have created one before it was suspended). Appends under g_table and
// re-checks g_abort there, so a thread is either in the table (the coordinator resumes it on abort) or
// resumed here.
Suspend SuspendOthers(DWORD coordinator, Result* res) {
    const DWORD me = GetCurrentThreadId();

    for (int pass = 0; pass < 16; ++pass) {
        bool fresh = false, full = false;
        const bool ok = ForEachThread(
            [&](DWORD tid) {
                if (res->aborted) {
                    return false;
                }
                if (tid == me || tid == coordinator || Known(tid)) {
                    return true;
                }

                if (g_count >= kMaxThreads) {
                    full = true;
                    return false;
                }

                Working(tid, 'O');
                const HANDLE h =
                    OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);

                if (!h) {
                    ++res->skipped;
                    return true;
                }

                Working(tid, 'S');
                const DWORD prev =
                    g_suspendHook ? DWORD(g_suspendHook(h, tid)) : SuspendThread(h);  // may block (WoW64)

                InterlockedIncrement(&g_progress);
                AcquireSRWLockExclusive(&g_table);

                if (Aborted()) {
                    ReleaseSRWLockExclusive(&g_table);
                    if (prev != DWORD(-1)) {
                        ResumeThread(h);
                    }
                    CloseHandle(h);
                    res->aborted = true;
                    return false;
                }

                if (prev == DWORD(-1)) {
                    ReleaseSRWLockExclusive(&g_table);
                    CloseHandle(h);
                    ++res->skipped;
                    return true;
                }

                g_frozen[g_count] = {tid, h};
                ++g_count;
                ReleaseSRWLockExclusive(&g_table);
                fresh = true;

                return true;
            },
            &res->grow);
        if (res->aborted) {
            return Suspend::Failed;
        }
        if (!ok) {
            return res->grow ? Suspend::Grow : Suspend::Failed;
        }
        if (full) {
            return Suspend::TooMany;
        }
        if (!fresh) {
            return Suspend::Ok;
        }
    }

    return Suspend::Ok;  // still new threads after 16 passes: proceed with what is suspended
}

// Reader. GetThreadContext also waits until the suspension has taken effect (SuspendThread is asynchronous).
void FindBusy(const Rs7Range* r, size_t n, Result* res) {
    for (size_t i = 0; i < g_count; ++i) {
        CONTEXT c;

        std::memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        Working(g_frozen[i].tid, 'C');
        const BOOL got = g_contextHook ? BOOL(g_contextHook(g_frozen[i].h, g_frozen[i].tid, &c))
                                       : GetThreadContext(g_frozen[i].h, &c);  // may block (WoW64)

        InterlockedIncrement(&g_progress);

        if (Aborted()) {  // the coordinator resumed the table: c is stale, the attempt is void
            res->aborted = true;
            return;
        }

        if (!got) {
            continue;  // exiting thread: runs no user code
        }

        for (size_t k = 0; k < n; ++k) {
            if (Rs7_FreezeRangeBusy(r[k], c.Eip)) {
                res->busy = true;
                res->busyTid = g_frozen[i].tid;
                res->busyEip = c.Eip;
                res->busyRange = k;
                return;
            }
        }
    }
}

DWORD WINAPI Reader(void*) {
    SetEvent(g_done);  // up and running (Init waits for this)

    for (;;) {
        WaitForSingleObject(g_go, INFINITE);
        Result res;

        Working(0, 'L');
        res.s = SuspendOthers(g_job.coordinator, &res);
        if (!res.aborted && res.s == Suspend::Ok) {
            FindBusy(g_job.ranges, g_job.n, &res);
        }

        if (res.aborted) {  // the coordinator resumed every thread in the table; the handles are ours to close
            AcquireSRWLockExclusive(&g_table);

            for (size_t i = 0; i < g_count; ++i) {
                CloseHandle(g_frozen[i].h);
            }

            g_count = 0;
            ReleaseSRWLockExclusive(&g_table);
        }

        Working(0, 0);
        g_res = res;
        InterlockedExchange(&g_readerBusy, 0);
        SetEvent(g_done);
    }
}

// ---- coordinator ----
enum class Run { Done, Stalled, Wedged };

double NowMs() {
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);

    return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}

// Hands one suspend + check job to the reader and watches it. On a stall resumes the table (under g_table,
// with g_abort set, so the reader resumes anything it suspends afterwards) and gives the reader graceMs.
Run RunJob(const Rs7Range* ranges, size_t n, Rs7FreezeStats* st, Result* res) {
    g_job.coordinator = GetCurrentThreadId();
    g_job.ranges = ranges;
    g_job.n = n;
    InterlockedExchange(&g_abort, 0);
    ResetEvent(g_done);  // a wedged job that finished later left it signalled
    InterlockedExchange(&g_readerBusy, 1);
    SetEvent(g_go);
    LONG last = InterlockedCompareExchange(&g_progress, 0, 0);
    double lastT = NowMs();

    for (;;) {
        if (WaitForSingleObject(g_done, 2) == WAIT_OBJECT_0) {
            *res = g_res;
            return Run::Done;
        }

        const LONG p = InterlockedCompareExchange(&g_progress, 0, 0);
        const double now = NowMs();

        if (p != last) {
            last = p;
            lastT = now;
            continue;
        }

        if (now - lastT < double(g_stallMs)) {
            continue;
        }

        // Stall: the reader is stuck in SuspendThread / GetThreadContext. Release everything we hold.
        Rs7FreezeStats::Event ev{};

        ev.op = char(InterlockedCompareExchange(&g_curOp, 0, 0));
        ev.tid = static_cast<unsigned long>(InterlockedCompareExchange(&g_curTid, 0, 0));
        ev.waitMs = float(now - lastT);
        AcquireSRWLockExclusive(&g_table);
        InterlockedExchange(&g_abort, 1);

        for (size_t i = 0; i < g_count; ++i) {
            ResumeThread(g_frozen[i].h);  // the reader closes them
        }

        ReleaseSRWLockExclusive(&g_table);
        const double t1 = NowMs();
        const bool back = WaitForSingleObject(g_done, g_graceMs) == WAIT_OBJECT_0;

        ev.releaseMs = back ? float(NowMs() - t1) : -1.0f;
        ++st->stalls;
        if (st->nEvents < Rs7FreezeStats::kMaxEvents) {
            st->events[st->nEvents++] = ev;
        }
        if (!back) {
            return Run::Wedged;
        }

        // The reader may have finished just before seeing g_abort (it then did not close the table): close
        // whatever is left. Every entry was resumed above; an empty table makes this a no-op.
        AcquireSRWLockExclusive(&g_table);

        for (size_t i = 0; i < g_count; ++i) {
            CloseHandle(g_frozen[i].h);
        }

        g_count = 0;
        ReleaseSRWLockExclusive(&g_table);
        *res = g_res;
        return Run::Stalled;
    }
}

// Every other thread is suspended and none stands in a range: runs fn, flushes the ranges and resumes.
void WriteFrozen(const Rs7Range* ranges, size_t n, Rs7FrozenFn fn, void* ctx, const LARGE_INTEGER& a,
                 Rs7FreezeStats* st) {
    LARGE_INTEGER b;

    fn(ctx);

    for (size_t k = 0; k < n; ++k) {
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(ranges[k].lo), ranges[k].hi - ranges[k].lo);
    }

    ResumeAll();
    QueryPerformanceCounter(&b);
    st->frozenMs = Ms(a, b);
}

// Freezes and retries until fn ran (true), the deadline passed or the freeze failed for good. Caller holds g_lock.
bool FreezeAndRun(const Rs7Range* ranges, size_t n, Rs7FrozenFn fn, void* ctx, Rs7FreezeStats* st,
                  unsigned deadlineMs) {
    const DWORD start = GetTickCount();
    unsigned backoff = 1;

    for (;;) {
        Result res;
        LARGE_INTEGER a;

        QueryPerformanceCounter(&a);
        const Run run = RunJob(ranges, n, st, &res);

        if (run == Run::Wedged) {  // nothing written; the table stays with the reader until it returns
            st->wedged = true;
            st->lastWasStall = true;
            return false;
        }

        if (run == Run::Stalled) {  // everything resumed, handles closed by the reader
            st->lastWasStall = true;

            if (GetTickCount() - start >= deadlineMs) {
                st->timedOut = true;
                return false;
            }

            Sleep(backoff);
            backoff = backoff < 64 ? backoff * 4 : 100;
            continue;
        }

        if (res.s == Suspend::Grow) {
            ResumeAll();
            if (!EnsureBuffer(res.grow)) {
                return false;
            }

            continue;
        }

        if (res.s == Suspend::TooMany || res.s == Suspend::Failed) {
            st->tooMany = res.s == Suspend::TooMany;
            ResumeAll();
            return false;
        }

        st->threads = unsigned(g_count);
        st->skipped = res.skipped;

        if (!res.busy) {
            WriteFrozen(ranges, n, fn, ctx, a, st);
            return true;
        }

        st->busyTid = res.busyTid;
        st->busyEip = res.busyEip;
        st->busyRange = res.busyRange;
        st->lastWasStall = false;
        ResumeAll();
        ++st->retries;

        if (GetTickCount() - start >= deadlineMs) {
            st->timedOut = true;
            return false;
        }

        Sleep(1);
    }
}
}  // namespace

bool Rs7_RunFrozen(const Rs7Range* ranges, size_t n, Rs7FrozenFn fn, void* ctx, Rs7FreezeStats* out,
                   unsigned deadlineMs) {
    Rs7FreezeStats st;

    AcquireSRWLockExclusive(&g_lock);

    if (InterlockedCompareExchange(&g_readerBusy, 0, 0)) {  // still blocked from an earlier freeze
        ReleaseSRWLockExclusive(&g_lock);
        st.readerBusy = true;
        if (out) {
            *out = st;
        }
        return false;
    }

    if (!Init()) {
        ReleaseSRWLockExclusive(&g_lock);
        st.timedOut = true;
        if (out) {
            *out = st;
        }
        return false;
    }

    LARGE_INTEGER t0, b;

    QueryPerformanceCounter(&t0);
    const bool done = FreezeAndRun(ranges, n, fn, ctx, &st, deadlineMs);

    QueryPerformanceCounter(&b);
    st.totalMs = Ms(t0, b);
    ReleaseSRWLockExclusive(&g_lock);
    if (out) {
        *out = st;
    }

    return done;
}

void Rs7_FreezeDescribe(const Rs7FreezeStats& st, char* out, size_t cap) {
    int w = 0;

    if (st.readerBusy) {
        w = std::snprintf(
            out, cap, "freeze: REFUSED - the thread reader is still blocked from an earlier freeze; nothing written");
    } else if (st.wedged) {
        w = std::snprintf(
            out, cap,
            "freeze: WEDGED after %.1f ms - the thread reader did not return within %u ms after every thread was "
            "resumed; nothing written, later freezes refused until it returns",
            st.totalMs, g_graceMs);
    } else if (st.tooMany) {
        w = std::snprintf(out, cap, "freeze: more than %u threads - nothing written", unsigned(kMaxThreads));
    } else if (st.timedOut && st.lastWasStall) {
        w = std::snprintf(
            out, cap,
            "freeze: DEADLINE after %.1f ms, %u stalls and %u retries - thread control kept stalling; nothing written",
            st.totalMs, st.stalls, st.retries);
    } else if (st.timedOut) {
        w = std::snprintf(out, cap,
                          "freeze: DEADLINE after %.1f ms and %u retries - thread %lu stayed at %08X inside range %u; "
                          "nothing written",
                          st.totalMs, st.retries, st.busyTid, unsigned(st.busyEip), unsigned(st.busyRange));
    } else {
        w = std::snprintf(out, cap,
                          "freeze: %u threads suspended for %.3f ms, %u retries, %u stalls (%.1f ms total, %u skipped)",
                          st.threads, st.frozenMs, st.retries, st.stalls, st.totalMs, st.skipped);
    }

    for (unsigned i = 0; i < st.nEvents && w >= 0 && size_t(w) < cap; ++i) {
        const auto& e = st.events[i];
        const char* op = e.op == 'S'   ? "SuspendThread"
                         : e.op == 'C' ? "GetThreadContext"
                         : e.op == 'O' ? "OpenThread"
                         : e.op == 'L' ? "thread listing"
                                       : "job pickup";
        int k;

        if (e.releaseMs >= 0) {
            k = std::snprintf(
                out + w, cap - size_t(w),
                "; stall %u: %s(thread %lu) no progress for %.0f ms -> all resumed, reader back after %.1f ms", i + 1,
                op, e.tid, double(e.waitMs), double(e.releaseMs));
        } else {
            k = std::snprintf(out + w, cap - size_t(w),
                              "; stall %u: %s(thread %lu) no progress for %.0f ms -> all resumed, reader NOT back",
                              i + 1, op, e.tid, double(e.waitMs));
        }

        if (k < 0) {
            break;
        }

        w += k;
    }

    if (st.stalls > st.nEvents && w >= 0 && size_t(w) < cap) {
        std::snprintf(out + w, cap - size_t(w), "; +%u more stalls", st.stalls - st.nEvents);
    }
}

size_t Rs7_FreezeListThreads(unsigned long* out, size_t cap) {
    AcquireSRWLockExclusive(&g_lock);
    size_t n = 0;

    if (!InterlockedCompareExchange(&g_readerBusy, 0, 0) && Init()) {
        for (int tries = 0; tries < 4; ++tries) {
            n = 0;
            ULONG grow = 0;
            if (ForEachThread(
                    [&](DWORD tid) {
                        if (n < cap) {
                            out[n] = tid;
                        }
                        ++n;

                        return true;
                    },
                    &grow)) {
                break;
            }

            n = 0;
            if (!grow || !EnsureBuffer(grow)) {
                break;
            }
        }
    }

    ReleaseSRWLockExclusive(&g_lock);

    return n < cap ? n : cap;
}

void Rs7_FreezeSetTestHooks(Rs7FreezeSuspendFn suspend, Rs7FreezeContextFn context) {
    g_suspendHook = suspend;
    g_contextHook = context;
}

void Rs7_FreezeSetTiming(unsigned stallMs, unsigned graceMs) {
    g_stallMs = stallMs ? stallMs : 250;
    g_graceMs = graceMs ? graceMs : 2000;
}

bool Rs7_FreezeReaderBusy() {
    return InterlockedCompareExchange(&g_readerBusy, 0, 0) != 0;
}
