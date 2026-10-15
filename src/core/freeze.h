#pragma once
#include <cstddef>
#include <cstdint>

// Review #9: code patches under a thread freeze, the way Detours does it.
//
// Rs7_RunFrozen suspends every other thread of the process, checks that none of them stands inside one of
// the byte ranges about to be written (strict interior lo < EIP < hi: a thread exactly at lo has not fetched
// anything yet and will fetch the new bytes), calls fn, flushes the instruction cache over the ranges and
// resumes everything. If a thread is inside a range, everything is resumed, the caller sleeps 1 ms and tries
// again until deadlineMs; then it gives up without calling fn (timedOut, busyTid/busyEip/busyRange say who).
//
// Threads that are suspended: ALL others except the freeze's own reader thread, including our own pipe /
// ramp / asset-dump workers, the game's main/render thread, its loader threads (Res_WorkerLoopA/B,
// AssetLoaderThread), audio (RS_ASIO / Wwise: a startup patch costs at most one audio buffer) and thread-pool
// workers. Only the calling thread runs (the reader is parked in a wait).
// Threads that cannot be opened or suspended (exiting) are counted in `skipped` and cannot run user code.
//
// Deadlock rules for fn (a suspended thread may hold the process heap lock, the loader lock, the CRT/stdio
// locks or our log's lock): NO heap allocation (no new / std::string / std::vector growth / std::function
// construction), NO logging, NO LoadLibrary / GetProcAddress, NO waits on locks. Prepare everything before;
// log the result after. Rs7_Protect (direct syscall, or VirtualProtect under Wine), FlushInstructionCache,
// Interlocked* and memcpy are safe. The thread list buffer is VirtualAlloc'ed before any thread is
// suspended; listing uses NtQuerySystemInformation (no heap).
//
// Trampolines (Rs7Mid stubs, fncopy copies) are never freed or rewritten after preparation (process lifetime),
// so a thread inside one is harmless for both a write and a rollback: it continues at site + length, which no
// write touches. Return addresses into a rewritten range are not checked (Detours does not either); sitecheck
// keeps Jmp sites to whole instructions.
//
// Threads created after the last listing pass (kernel thread-pool workers) are not suspended; they start in
// RtlUserThreadStart, never inside a game site.
//
// Bounded thread control (startup hang, smoke frame-2). In a WoW64 process SuspendThread and GetThreadContext on
// another thread of the same process are NOT plain kernel calls: wow64!Wow64NtGetContextThread suspends the target once
// more through ntdll!RtlWow64SuspendThread, which (feature SuspendThread2) runs a hidden worker thread
// (RtlpWow64SuspendThreadWorker -> wow64!Wow64SuspendLocalThread, flags 0 for a same-process target) and
// waits for it without a timeout. Wow64SuspendLocalThread serialises on a process-wide word
// (Wow64ActiveSuspendWorkerTeb) and, if the target stands in a WoW64 state it considers unsafe, resumes it and waits
// for it to reach a safe point - in 360 ms slices, with no deadline. A target (or a lock owner) that we hold suspended
// can never get there, so the call can block for good. Therefore every SuspendThread / GetThreadContext of a freeze
// runs on a dedicated reader thread (never suspended, idle in a wait while fn runs) and the calling thread watches its
// progress. No progress for stallMs: every thread suspended so far is resumed (that lets the target and any lock owner
// run, which is what the WoW64 wait needs), nothing is written, the reader is given graceMs to come back, then the
// attempt is retried after a short back-off until deadlineMs. A reader that does not come back within
// graceMs is "wedged": the call fails (nothing written), and later calls fail at once until it returns.
// Every stall is recorded in Rs7FreezeStats::events and printed by Rs7_FreezeDescribe.
struct Rs7Range {
    uintptr_t lo, hi;  // [lo, hi)
};

// Pure predicate shared with FindBusy; endpoints are deliberately excluded.
inline bool Rs7_FreezeRangeBusy(const Rs7Range& range, uintptr_t eip) {
    return eip > range.lo && eip < range.hi;
}

struct Rs7FreezeStats {
    unsigned threads = 0;  // suspended in the successful (or last) attempt
    unsigned skipped = 0;  // could not be opened / suspended (exiting threads)
    unsigned retries = 0;  // attempts that found a thread inside a range
    double frozenMs = 0;   // first SuspendThread .. last ResumeThread of the attempt that ran fn
    double totalMs = 0;    // including the retries
    bool timedOut = false;
    bool tooMany = false;  // more threads than the fixed table holds (fn not called)
    unsigned long busyTid = 0;
    uintptr_t busyEip = 0;
    size_t busyRange = 0;  // index into ranges of the last conflict
    // Bounded thread control (see above).
    unsigned stalls = 0;        // attempts abandoned because SuspendThread / GetThreadContext made no progress
    bool lastWasStall = false;  // the last failed attempt was a stall (not a thread inside a range)
    bool wedged = false;        // the reader did not come back within graceMs after resuming everything
    bool readerBusy = false;    // refused at once: the reader is still blocked from an earlier freeze

    struct Event {
        char op;            // 'S' SuspendThread, 'C' GetThreadContext, 'O' OpenThread, 'L' listing, 0 pickup
        unsigned long tid;  // the thread the reader was working on
        float waitMs;       // no progress for this long before everything was resumed
        float releaseMs;    // resume-all .. reader back (-1: not back within graceMs)
    };

    static constexpr unsigned kMaxEvents = 8;
    Event events[kMaxEvents] = {};
    unsigned nEvents = 0;  // recorded events (<= kMaxEvents; stalls counts all)
};

using Rs7FrozenFn = void (*)(void* ctx);

// Must not be called with a lock held that another thread needs to reach its next instruction boundary.
// Not reentrant (one global thread table): calls are serialised by an internal lock.
bool Rs7_RunFrozen(const Rs7Range* ranges, size_t n, Rs7FrozenFn fn, void* ctx, Rs7FreezeStats* st,
                   unsigned deadlineMs = 2000);

// One line for the log, e.g. "freeze: 63 threads suspended for 0.41 ms, 0 retries (0 skipped)".
void Rs7_FreezeDescribe(const Rs7FreezeStats& st, char* out, size_t cap);

// Tests: thread ids of this process (own id included), straight from NtQuerySystemInformation.
size_t Rs7_FreezeListThreads(unsigned long* out, size_t cap);

// Tests: replace the thread-control calls the reader makes (nullptr = the real SuspendThread /
// GetThreadContext) and the stall / grace timings (defaults 250 ms / 2000 ms). Not thread-safe against a
// running freeze.
using Rs7FreezeSuspendFn = unsigned long (*)(void* thread, unsigned long tid);
using Rs7FreezeContextFn = int (*)(void* thread, unsigned long tid, void* context /* CONTEXT* */);
void Rs7_FreezeSetTestHooks(Rs7FreezeSuspendFn suspend, Rs7FreezeContextFn context);
void Rs7_FreezeSetTiming(unsigned stallMs, unsigned graceMs);
// Tests: true while the reader is still inside a job (a wedged call has not returned yet).
bool Rs7_FreezeReaderBusy();
