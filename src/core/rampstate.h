#pragma once
#include <cstdint>

// Step 2 ramp-swap state (plan D6): the candidate table (stock ramp textures found in the engine's
// texture list), the V6 rule that drives g_rs7_v6, and counters logged at song setup.
// Pure bookkeeping: no D3D calls, candidates' texture pointers are compared, never dereferenced.

enum class Rs7Family : uint8_t { Note, Additive };
// Pending: found by the worker, never processed. Busy: the render thread is processing it (Claim).
// Active: our copy is bound in its place (stock + ours referenced). Refused: never swapped. Retry:
// processing failed transiently, again at retryAtMs. Retired: free slot (stale entry; d3d = nullptr).
enum class Rs7CandState : uint8_t { Pending, Active, Refused, Retry, Busy, Retired };

struct Rs7Cand {         // written by the worker (Pending, Retire) and the render thread (rest)
    void* volatile d3d;  // stock IDirect3DBaseTexture9* (raw until Active: compare only)
    Rs7Family family;
    volatile Rs7CandState state;
    void* stock;  // Active: IDirect3DTexture9* we hold a reference on
    void* ours;   // Active: our IDirect3DTexture9* (one reference per candidate)
    uint64_t hash;
    unsigned retries;
    uint32_t retryAtMs;
    uintptr_t obj;       // engine source texture it was found on (revalidation)
    volatile long gone;  // worker: an Active entry missing from a clean walk (render thread retires it)
    char path[120];      // platform filename, for the log
};

constexpr int kRs7MaxCands = 16;
// Writers (Add, Claim, Retire, ResetForTests) are serialised by a lock; Find is lock-free. Add reuses a
// Retired slot before appending; a slot's fields are written before its d3d pointer is published.
int Rs7Cand_Add(void* d3d, Rs7Family f, const char* path, uintptr_t obj = 0);  // -1 full; existing index if present
Rs7Cand* Rs7Cand_Find(const void* d3d);                                        // nullptr if unknown (no deref of d3d)
int Rs7Cand_Count();                                                           // slots in use (Retired included)
Rs7Cand* Rs7Cand_At(int i);                                                    // nullptr outside [0, Count)
// Render thread: Pending/Retry -> Busy iff c still holds d3d. False: leave it alone.
bool Rs7Cand_Claim(Rs7Cand* c, const void* d3d);
// -> Retired (d3d = nullptr, slot reusable) iff c still holds d3d. byOwner = false (the worker): only
// Pending/Refused/Retry. byOwner = true (the render thread, which owns Busy and Active entries; it
// releases stock/ours first): any live state. True if retired.
bool Rs7Cand_Retire(Rs7Cand* c, const void* d3d, bool byOwner);
// D6: on iff enabled && deviceOk && >= 1 Note Active && no Refused/Retry of either family.
bool Rs7Ramp_V6Rule(bool enabled, bool deviceOk);
// Applies the rule to g_rs7_v6 (both modes, atomic dword stores); logs "ramp: V6 ON/OFF (reason)" on change
// (inside the lock, so lines from concurrent callers keep the order of the stores).
void Rs7Ramp_UpdateV6(bool enabled, bool deviceOk, const char* reason);
// Counters (Interlocked) and the S4 log line.
void Rs7Ramp_CountSwap();
void Rs7Ramp_CountHidden();
void Rs7Ramp_CountFault();
// "ramp stats: swaps=%u hidden=%u faults=%u candidates %u (active %u, refused %u, retry %u, pending %u) v6=%s";
// candidates = live entries (Retired excluded), pending includes Busy.
void Rs7Ramp_LogStats();
// Tests: empties the table, zeroes the counters and g_rs7_v6 (no log line).
void Rs7Cand_ResetForTests();
