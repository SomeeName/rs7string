#pragma once
#include <cstddef>
#include <cstdint>

// Code patches under a thread freeze, the way Detours does it.
//
// Rsxr_RunFrozen suspends every other thread of the process, checks that none of them stands inside one of
// the byte ranges about to be written (strict interior lo < EIP < hi: a thread exactly at lo has not fetched
// anything yet and will fetch the new bytes), calls fn, flushes the instruction cache over the ranges and
// resumes everything. If a thread is inside a range, everything is resumed, the caller sleeps 1 ms and tries
// again until deadlineMs; then it gives up without calling fn (timedOut, busyTid/busyEip/busyRange say who).
//
// Threads that are suspended: ALL others except the freeze's own reader thread, including our own pipe /
// ramp / asset-dump workers, the game's main/render thread, its loader threads (Res_WorkerLoopA/B,
// AssetLoaderThread), audio (RS_ASIO / Wwise: a startup patch costs at most one audio buffer) and thread-pool
// workers. Only the calling thread runs (the reader is parked in a wait).
// Failed thread-control calls are skipped only after termination is confirmed; otherwise the freeze fails.
//
// Deadlock rules for fn (a suspended thread may hold the process heap lock, the loader lock, the CRT/stdio
// locks or our log's lock): NO heap allocation (no new / std::string / std::vector growth / std::function
// construction), NO logging, NO LoadLibrary / GetProcAddress, NO waits on locks. Prepare everything before;
// log the result after. Rsxr_Protect (direct syscall, or VirtualProtect under Wine), FlushInstructionCache,
// Interlocked* and memcpy are safe. The thread list buffer is VirtualAlloc'ed before any thread is
// suspended; listing uses NtGetNextThread (no heap; fallback NtQuerySystemInformation).
//
// Trampolines (RsxrMid stubs, fncopy copies) are never freed or rewritten after preparation (process lifetime),
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
// progress. No progress for stallMs inside GetThreadContext: only that target is resumed (it can now reach its safe
// point; the reader then suspends it again and reads it again, at most 8 times per attempt, and nothing is written
// unless every listed thread is held again and enumeration/context validation have converged). No progress for
// another stallMs, or inside SuspendThread / the
// listing: every thread suspended so far is resumed (that lets the target and any lock owner
// run, which is what the WoW64 wait needs), nothing is written, the reader is given graceMs to come back, then the
// attempt is retried after a short back-off until deadlineMs. A reader that does not come back within
// graceMs is "wedged": the call fails (nothing written), and later calls fail at once until it returns.
// Every stall is recorded in RsxrFreezeStats::events and printed by Rsxr_FreezeDescribe.
struct RsxrRange {
    uintptr_t lo, hi;  // [lo, hi)
};

// Pure predicate shared with FindBusy; endpoints are deliberately excluded.
inline bool Rsxr_FreezeRangeBusy(const RsxrRange& range, uintptr_t eip) {
    return eip > range.lo && eip < range.hi;
}

struct RsxrFreezeStats {
    static constexpr unsigned kMaxThreads = 1024;  // the fixed thread table
    unsigned threads = 0;                          // suspended in the successful (or last) attempt
    unsigned skipped = 0;                          // could not be opened / suspended (exiting threads)
    unsigned retries = 0;                          // attempts that found a thread inside a range
    double frozenMs = 0;  // first SuspendThread .. last ResumeThread of the attempt that ran fn
    double totalMs = 0;   // including the retries
    bool timedOut = false;
    bool tooMany = false;  // more threads than the fixed table holds (fn not called)
    unsigned long busyTid = 0;
    uintptr_t busyEip = 0;
    size_t busyRange = 0;  // index into ranges of the last conflict
    // Bounded thread control (see above).
    unsigned stalls = 0;        // attempts abandoned because SuspendThread / GetThreadContext made no progress
    unsigned released = 0;      // GetThreadContext targets let run alone to their WoW64 safe point (no abandon)
    bool lastWasStall = false;  // the last failed attempt was a stall (not a thread inside a range)
    bool wedged = false;        // the reader did not come back within graceMs after resuming everything
    bool readerBusy = false;    // refused at once: the reader is still blocked from an earlier freeze
    unsigned graceMs = 0;       // the grace the reader had (for the WEDGED line)

    struct Event {
        char op;            // 'S' SuspendThread, 'C' GetThreadContext, 'L' listing, 0 pickup
        unsigned long tid;  // the thread the reader was working on
        float waitMs;       // no progress for this long before everything was resumed
        float releaseMs;    // resume-all .. reader back (-1: not back within graceMs)
    };

    static constexpr unsigned kMaxEvents = 8;
    Event events[kMaxEvents] = {};
    unsigned nEvents = 0;  // recorded events (<= kMaxEvents; stalls counts all)
};

using RsxrFrozenFn = void (*)(void* ctx);

// Must not be called with a lock held that another thread needs to reach its next instruction boundary.
// Not reentrant (one global thread table): calls are serialised by an internal lock.
bool Rsxr_RunFrozen(const RsxrRange* ranges, size_t n, RsxrFrozenFn fn, void* ctx, RsxrFreezeStats* st,
                    unsigned deadlineMs = 2000);

// Only from fn inside a successful Rsxr_RunFrozen: inspects the already validated, held thread contexts.
// Includes lo (unlike patch-byte conflicts): a constructor at its entry is already in flight.
// Checks EIP and possible return addresses from ESP to the thread's real StackBase.
// Unknown bounds, invalid ESPs and unreadable stacks return true; callers must refuse writes on true.
// No allocation, thread-control calls or locks.
enum class RsxrFrozenReason {
    None,
    NoFs,
    SelectorFailure,
    TibMismatch,
    EspOutside,
    Unaligned,
    Unreadable,
    GuardPage,
    AllocationBoundary,
    ShortRegion,
    CopyFailed,
    Eip,
    ReturnAddress
};

// Plain storage only: capture inside the freeze, describe/log after every held thread is resumed.
// address/value identify the failed read, selector, TIB.Self, allocation, EIP or stack word.
struct RsxrFrozenRefusal {
    RsxrFrozenReason reason = RsxrFrozenReason::None;
    unsigned long tid = 0, fs = 0, error = 0, state = 0, protect = 0;
    uintptr_t eip = 0, esp = 0, stackLimit = 0, stackBase = 0, teb = 0;
    uintptr_t address = 0, value = 0;
    RsxrRange range{};
};

bool Rsxr_FrozenThreadCallsRange(const RsxrRange& range, RsxrFrozenRefusal* refusal = nullptr);
const char* Rsxr_FrozenReasonName(RsxrFrozenReason reason);

// One line for the log, e.g. "freeze: 63 threads suspended for 0.41 ms, 0 retries (0 skipped)".
void Rsxr_FreezeDescribe(const RsxrFreezeStats& st, char* out, size_t cap);

// Tests: thread ids of this process (own id included), from NtGetNextThread (NtQuerySystemInformation without it).
size_t Rsxr_FreezeListThreads(unsigned long* out, size_t cap);

// Tests: replace the thread-control calls the reader makes (nullptr = the real SuspendThread /
// GetThreadContext) and the stall / grace timings (defaults 250 ms / 2000 ms). Not thread-safe against a
// running freeze.
using RsxrFreezeSuspendFn = unsigned long (*)(void* thread, unsigned long tid);
using RsxrFreezeContextFn = int (*)(void* thread, unsigned long tid, void* context /* CONTEXT* */);
void Rsxr_FreezeSetTestHooks(RsxrFreezeSuspendFn suspend, RsxrFreezeContextFn context);
void Rsxr_FreezeSetTiming(unsigned stallMs, unsigned graceMs);
// Tests: true while the reader is still inside a job (a wedged call has not returned yet).
bool Rsxr_FreezeReaderBusy();
// Tests: true once the coordinator has abandoned the running attempt (resumed every thread it held).
bool Rsxr_FreezeAbortedForTests();
// Tests: pause completion publication (nullptr = no hook). Set only while the reader is idle.
void Rsxr_FreezeSetCompletionHookForTests(void (*hook)());
