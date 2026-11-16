#include "platform/freeze_completion.h"
#include "platform/freeze_stack.h"
#include "platform/protect.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <winternl.h>

namespace {
constexpr size_t kMaxThreads = Rs7FreezeStats::kMaxThreads;
constexpr LONG kInfoLengthMismatch = LONG(0xC0000004);
constexpr LONG kNoMoreEntries = LONG(0x8000001A);
constexpr ACCESS_MASK kThreadAccess =
    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | SYNCHRONIZE;
constexpr unsigned kMaxResuspends = 8;  // per job: threads let run to their WoW64 safe point and suspended again

using NtQsi = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtGnt = LONG(NTAPI*)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, _Out_ PHANDLE);

SRWLOCK g_lock = SRWLOCK_INIT;  // one freeze at a time (the tables below are shared)
NtGnt g_gnt = nullptr;          // NtGetNextThread: this process's threads only (preferred)
NtQsi g_qsi = nullptr;          // fallback without NtGetNextThread (old Wine): the system-wide process list
uint8_t* g_buf = nullptr;       // fallback only; touched by the reader during a job only; resized while it is idle
ULONG g_bufSize = 0;

struct Frozen {
    DWORD tid;
    HANDLE h;
    bool held;  // we hold one suspension (false: the coordinator let it run to its WoW64 safe point)
    bool gone;  // termination confirmed: nothing of ours to resume
    Rs7FrozenContext context;
};

// Reader appends during a job; coordinator reads only while idle or under g_table on abort. Neither is suspended.
Frozen g_frozen[kMaxThreads] = {};
size_t g_count = 0;
SRWLOCK g_table = SRWLOCK_INIT;

unsigned g_stallMs = 250;   // no progress for this long: resume everything (normal: < 1 ms per call)
unsigned g_graceMs = 2000;  // then wait this long for the reader to come back
Rs7FreezeSuspendFn g_suspendHook = nullptr;
Rs7FreezeContextFn g_contextHook = nullptr;

HANDLE g_reader = nullptr, g_go = nullptr;
Rs7FreezeCompletion g_completion;
DWORD g_readerTid = 0;
bool g_readerReady = false;    // the reader has signalled its completion event once after starting
volatile LONG g_abort = 0;     // the coordinator gave up on the job and resumed the table
volatile LONG g_progress = 0;  // +1 per completed thread-control call
volatile LONG g_curTid = 0;    // the thread the reader is working on
volatile LONG g_curOp = 0;     // 'S' / 'C'

enum class Suspend { Ok, Grow, TooMany, Failed };
enum class Visit { Next, NextKept, Stop };  // Stop: the visitor has closed or kept the handle itself

struct Job {
    LONG generation = 0;
    DWORD coordinator = 0;
    const Rs7Range* ranges = nullptr;
    size_t n = 0;
};

struct Result {
    Suspend s = Suspend::Ok;
    unsigned skipped = 0;
    ULONG grow = 0;
    bool busy = false, aborted = false;
    unsigned resuspends = 0;
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
    if (!g_gnt && !g_qsi) {
        const HMODULE nt = GetModuleHandleW(L"ntdll.dll");

        g_gnt = nt ? reinterpret_cast<NtGnt>(GetProcAddress(nt, "NtGetNextThread")) : nullptr;
        if (!g_gnt) {
            g_qsi = nt ? reinterpret_cast<NtQsi>(GetProcAddress(nt, "NtQuerySystemInformation")) : nullptr;
        }
    }

    Rs7_ProtectMethod();  // resolves the protect syscall (file read, heap) now, never inside a freeze
    if (!g_go) {
        g_go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (!g_completion.done) {
        g_completion.done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (!g_reader && g_go && g_completion.done) {
        g_reader = CreateThread(nullptr, 64 * 1024, Reader, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &g_readerTid);
    }
    // Thread attach took ~0.4 s once at startup (smoke wmi-5): await readiness before starting any job.
    const HANDLE done = g_completion.done;  // a local the analyser tracks

    if (g_reader && done && !g_readerReady) {
        g_readerReady = WaitForSingleObject(done, 5000) == WAIT_OBJECT_0;
    }
    if ((!g_gnt && !g_qsi) || !g_reader || !g_readerReady) {
        return false;
    }
    if (g_gnt || InterlockedCompareExchange(&g_completion.busy, 0, 0)) {
        return true;  // no buffer needed, or it belongs to the reader
    }

    return EnsureBuffer(g_bufSize ? g_bufSize : 0x200000);
}

// Fallback: visit(tid) for this process's threads. False with needSize set if the system-list buffer is too small.
template <class F>
bool ForEachThreadId(F visit, ULONG* needSize) {
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

// Heap-free visit(tid, handle): Next closes the handle, NextKept leaves it to the visitor, Stop ends the walk.
// NtGetNextThread visits only this process; the system-list fallback can exceed the stall limit under load.
template <class F>
bool ForEachThread(F visit, ULONG* needSize) {
    *needSize = 0;

    if (g_gnt) {
        HANDLE cur = nullptr;
        bool owned = false;

        for (;;) {
            HANDLE next = nullptr;
            const LONG s = g_gnt(GetCurrentProcess(), cur, kThreadAccess, 0, 0, &next);

            if (owned) {
                CloseHandle(cur);
            }
            if (s < 0 || !next) {
                return s == kNoMoreEntries;
            }

            // C6001 "*next": a HANDLE is no data pointer; GetThreadId's _In_ makes /analyze want its pointee.
#pragma warning(suppress : 6001)
            const Visit v = visit(GetThreadId(next), next);

            if (v == Visit::Stop) {
                return true;
            }

            cur = next;
            owned = v == Visit::Next;
        }
    }

    return ForEachThreadId(
        [&](DWORD tid) {
            const HANDLE h = OpenThread(kThreadAccess, FALSE, tid);

            if (!h) {
                return visit(tid, nullptr) != Visit::Stop;
            }

            const Visit v = visit(tid, h);

            if (v == Visit::Next) {
                CloseHandle(h);
            }

            return v != Visit::Stop;
        },
        needSize);
}

void ResumeAll() {  // coordinator, reader idle
    for (size_t i = 0; i < g_count; ++i) {
        if (g_frozen[i].held && !g_frozen[i].gone) {
            ResumeThread(g_frozen[i].h);
        }
        CloseHandle(g_frozen[i].h);
    }

    g_count = 0;
}

bool AllHeld() {  // coordinator, reader idle
    for (size_t i = 0; i < g_count; ++i) {
        if (!g_frozen[i].held) {
            return false;
        }
    }

    return true;
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

bool Exited(HANDLE h) {
    return h && WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
}

// Reader repeats listing after contexts acknowledge suspensions; g_table/g_abort ensure late suspensions are undone.
Suspend SuspendOthers(DWORD coordinator, Result* res) {
    const DWORD me = GetCurrentThreadId();
    bool full = false, failed = false;
    const bool ok = ForEachThread(
        [&](DWORD tid, HANDLE h) {
            if (tid == me || tid == coordinator || Known(tid)) {
                return Visit::Next;
            }

            if (!h) {
                failed = true;  // no handle: termination cannot be established
                return Visit::Stop;
            }

            if (g_count >= kMaxThreads) {
                full = true;
                CloseHandle(h);
                return Visit::Stop;
            }

            Working(tid, 'S');
            const DWORD prev = g_suspendHook ? DWORD(g_suspendHook(h, tid)) : SuspendThread(h);  // may block (WoW64)

            InterlockedIncrement(&g_progress);
            AcquireSRWLockExclusive(&g_table);

            if (Aborted()) {
                ReleaseSRWLockExclusive(&g_table);
                if (prev != DWORD(-1)) {
                    ResumeThread(h);
                }
                CloseHandle(h);
                res->aborted = true;
                return Visit::Stop;
            }

            if (prev == DWORD(-1)) {
                ReleaseSRWLockExclusive(&g_table);

                if (!Exited(h)) {
                    failed = true;
                    CloseHandle(h);
                    return Visit::Stop;
                }

                ++res->skipped;
                return Visit::Next;
            }

            g_frozen[g_count] = {tid, h, true, false, {}};
            ++g_count;
            ReleaseSRWLockExclusive(&g_table);

            return Visit::NextKept;
        },
        &res->grow);
    if (res->aborted || failed) {
        return Suspend::Failed;
    }
    if (!ok) {
        return res->grow ? Suspend::Grow : Suspend::Failed;
    }
    if (full) {
        return Suspend::TooMany;
    }

    return Suspend::Ok;
}

// Reader: GetThreadContext acknowledges suspension; a thread released during the call must be suspended/read again.
void FindBusy(const Rs7Range* r, size_t n, Result* res) {
    for (size_t i = 0; i < g_count;) {
        if (g_frozen[i].gone) {
            ++i;
            continue;
        }

        CONTEXT c;

        std::memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_SEGMENTS;
        Working(g_frozen[i].tid, 'C');
        const BOOL got = g_contextHook ? BOOL(g_contextHook(g_frozen[i].h, g_frozen[i].tid, &c))
                                       : GetThreadContext(g_frozen[i].h, &c);  // may block (WoW64)

        InterlockedIncrement(&g_progress);

        if (Aborted()) {  // the coordinator resumed the table: c is stale, the attempt is void
            res->aborted = true;
            return;
        }

        AcquireSRWLockExclusive(&g_table);
        const bool held = g_frozen[i].held;

        ReleaseSRWLockExclusive(&g_table);

        if (!held) {  // c was taken while it ran: suspend it again and read it again
            if (++res->resuspends > kMaxResuspends) {
                return;  // the coordinator sees an entry not held and retries the attempt (AllHeld)
            }

            Working(g_frozen[i].tid, 'S');
            const DWORD prev =
                g_suspendHook ? DWORD(g_suspendHook(g_frozen[i].h, g_frozen[i].tid)) : SuspendThread(g_frozen[i].h);

            InterlockedIncrement(&g_progress);
            AcquireSRWLockExclusive(&g_table);

            if (Aborted()) {  // the coordinator resumed what it held; this suspension is ours to undo
                ReleaseSRWLockExclusive(&g_table);
                if (prev != DWORD(-1)) {
                    ResumeThread(g_frozen[i].h);
                }
                res->aborted = true;
                return;
            }

            g_frozen[i].gone = prev == DWORD(-1) && Exited(g_frozen[i].h);
            g_frozen[i].held = prev != DWORD(-1) || g_frozen[i].gone;
            ReleaseSRWLockExclusive(&g_table);

            if (prev == DWORD(-1)) {
                if (!g_frozen[i].gone) {
                    res->s = Suspend::Failed;
                    return;
                }

                ++i;
            }

            continue;
        }

        if (!got) {
            if (!Exited(g_frozen[i].h)) {
                res->s = Suspend::Failed;
                return;
            }

            AcquireSRWLockExclusive(&g_table);  // termination confirmed: runs no user code, the stack scan skips it
            g_frozen[i++].gone = true;
            ReleaseSRWLockExclusive(&g_table);
            continue;
        }

        g_frozen[i].context = Rs7FrozenContext::Capture(g_frozen[i].h, c.Eip, c.Esp, c.SegFs);

        for (size_t k = 0; k < n; ++k) {
            if (Rs7_FreezeRangeBusy(r[k], c.Eip)) {
                res->busy = true;
                res->busyTid = g_frozen[i].tid;
                res->busyEip = c.Eip;
                res->busyRange = k;
                return;
            }
        }

        ++i;
    }
}

DWORD WINAPI Reader(void*) {
    SetEvent(g_completion.done);  // up and running (Init waits for this)

    for (;;) {
        WaitForSingleObject(g_go, INFINITE);
        Result res;

        for (unsigned pass = 0; pass < 16; ++pass) {
            const size_t before = g_count;
            const unsigned resuspends = res.resuspends;

            Working(0, 'L');
            res.s = SuspendOthers(g_job.coordinator, &res);
            if (res.aborted || res.s != Suspend::Ok) {
                break;
            }

            FindBusy(g_job.ranges, g_job.n, &res);
            if (res.aborted || res.s != Suspend::Ok || res.busy || res.resuspends > kMaxResuspends ||
                (g_count == before && res.resuspends == resuspends)) {
                break;
            }

            res.s = Suspend::Failed;  // the pass limit must never allow an unvalidated thread set
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
        g_completion.Publish(g_job.generation);
    }
}

enum class Run { Done, Stalled, Wedged };

double NowMs() {
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);

    return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}

Run RunJob(const Rs7Range* ranges, size_t n, Rs7FreezeStats* st, Result* res) {
    g_job.coordinator = GetCurrentThreadId();
    g_job.ranges = ranges;
    g_job.n = n;
    InterlockedExchange(&g_abort, 0);
    g_job.generation = g_completion.Start();
    SetEvent(g_go);
    LONG last = InterlockedCompareExchange(&g_progress, 0, 0);
    double lastT = NowMs();
    bool released = false;  // the current GetThreadContext target was let run (once per stall)

    for (;;) {
        if (g_completion.Wait(g_job.generation, 2)) {
            *res = g_res;
            return Run::Done;
        }

        const LONG p = InterlockedCompareExchange(&g_progress, 0, 0);
        const double now = NowMs();

        if (p != last) {
            last = p;
            lastT = now;
            released = false;
            continue;
        }

        if (now - lastT < double(g_stallMs)) {
            continue;
        }

        // GetThreadContext stall: WoW64 waits for the target to reach a safe point, and we hold it. Let only that
        // thread run (the reader suspends and reads it again); everything else stays suspended.
        if (!released && InterlockedCompareExchange(&g_curOp, 0, 0) == 'C') {
            const DWORD tid = DWORD(InterlockedCompareExchange(&g_curTid, 0, 0));

            released = true;
            AcquireSRWLockExclusive(&g_table);

            for (size_t i = 0; i < g_count; ++i) {
                if (g_frozen[i].tid == tid && g_frozen[i].held && !g_frozen[i].gone) {
                    if (ResumeThread(g_frozen[i].h) != DWORD(-1)) {
                        g_frozen[i].held = false;
                        ++st->released;
                    }

                    break;
                }
            }

            ReleaseSRWLockExclusive(&g_table);
            lastT = now;
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
            if (g_frozen[i].held && !g_frozen[i].gone) {
                ResumeThread(g_frozen[i].h);  // the reader closes them
            }
            g_frozen[i].held = false;
        }

        ReleaseSRWLockExclusive(&g_table);
        const double t1 = NowMs();
        const bool back = g_completion.Wait(g_job.generation, g_graceMs);

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

        if (!AllHeld()) {  // a thread let run to its safe point (RunJob) kept escaping: retry the attempt
            st->lastWasStall = true;
            ++st->stalls;
            ResumeAll();

            if (GetTickCount() - start >= deadlineMs) {
                st->timedOut = true;
                return false;
            }

            Sleep(backoff);
            backoff = backoff < 64 ? backoff * 4 : 100;
            continue;
        }

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

bool Rs7_FrozenThreadCallsRange(const Rs7Range& range, Rs7FrozenRefusal* refusal) {
    return std::any_of(g_frozen, g_frozen + g_count,
                       [&](const Frozen& f) { return !f.gone && f.context.CallsRange(range, refusal, f.tid); });
}

bool Rs7_RunFrozen(const Rs7Range* ranges, size_t n, Rs7FrozenFn fn, void* ctx, Rs7FreezeStats* out,
                   unsigned deadlineMs) {
    Rs7FreezeStats st;

    AcquireSRWLockExclusive(&g_lock);

    if (InterlockedCompareExchange(&g_completion.busy, 0, 0)) {  // still blocked from an earlier freeze
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
    st.graceMs = g_graceMs;
    const bool done = FreezeAndRun(ranges, n, fn, ctx, &st, deadlineMs);

    QueryPerformanceCounter(&b);
    st.totalMs = Ms(t0, b);
    ReleaseSRWLockExclusive(&g_lock);
    if (out) {
        *out = st;
    }

    return done;
}

size_t Rs7_FreezeListThreads(unsigned long* out, size_t cap) {
    AcquireSRWLockExclusive(&g_lock);
    size_t n = 0;

    if (!InterlockedCompareExchange(&g_completion.busy, 0, 0) && Init()) {
        for (int tries = 0; tries < 4; ++tries) {
            n = 0;
            ULONG grow = 0;
            if (ForEachThread(
                    [&](DWORD tid, HANDLE) {
                        if (n < cap) {
                            out[n] = tid;
                        }
                        ++n;

                        return Visit::Next;
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
    return InterlockedCompareExchange(&g_completion.busy, 0, 0) != 0;
}

bool Rs7_FreezeAbortedForTests() {
    return Aborted();
}

void Rs7_FreezeSetCompletionHookForTests(void (*hook)()) {
    g_completion.hook = hook;
}
