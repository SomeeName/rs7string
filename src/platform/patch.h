#pragma once
#include <cstddef>
#include <cstdint>

// A failed cleanup can follow a successful write. Callers that roll back must retain both facts.
struct RsxrWriteResult {
    bool written;
    bool protectionRestored;

    RsxrWriteResult(bool changed, bool restored = true) : written(changed), protectionRestored(restored) {}

    operator bool() const { return written && protectionRestored; }
};

// Every writer makes the bytes writable, writes, explicitly restores protection and flushes the cache.
// A precondition failure has written == false; cleanup failure has protectionRestored == false.
RsxrWriteResult Rsxr_WriteBytes(uintptr_t at, const void* bytes, size_t len);
// Writes `original` back and reports success only if a read-back compare matches. Heap-free
// (safe inside a thread freeze).
bool Rsxr_RestoreBytes(uintptr_t at, const void* original, size_t len);
// E9 rel32 to dest, NOP-filled to `length` (5..16).
RsxrWriteResult Rsxr_WriteJmp(uintptr_t site, size_t length, uintptr_t dest);
// 5-byte jmp that must lie inside one aligned qword; written with one locked 8-byte exchange.
RsxrWriteResult Rsxr_WriteJmpAtomic8(uintptr_t site, uintptr_t dest);
// Retarget a rel32 operand at relAt of an instruction ending at insnEnd. atomic: one aligned
// 32-bit store (relAt must be 4-aligned).
RsxrWriteResult Rsxr_WriteRel32(uintptr_t relAt, uintptr_t insnEnd, uintptr_t dest, bool atomic);
// Aligned 4-byte pointer store into (possibly read-only) data, e.g. a vtable slot: one interlocked
// exchange, protection restored. An executable page stays executable while it is writable. *old (if
// non-null) gets the previous value even if protection cleanup fails after the exchange.
RsxrWriteResult Rsxr_WritePtrAtomic(uintptr_t at, uintptr_t value, uintptr_t* old);
// Two adjacent slots, each exchanged atomically. On failure, restore every slot changed so far (best effort).
// *failedSlot (optional) is 0 or 1. The pair itself is not an atomic exchange.
bool Rsxr_WritePtrPair(uintptr_t at, uintptr_t first, uintptr_t second, unsigned* failedSlot = nullptr);

// An 8-byte window holding [site, site + n) (n 1..8) that lies inside one 64-byte cache line: the
// aligned qword when it fits, else the lowest such start. False if none exists (the bytes straddle a
// cache line). A locked 8-byte access inside one cache line is atomic on P6 and later (Intel SDM
// vol. 3A "Guaranteed Atomic Operations"), aligned or not.
bool Rsxr_CasWindow(uintptr_t site, size_t n, uintptr_t* window);
enum class RsxrCasResult { Written, Mismatch, Unsupported, ProtectFailed, WrittenCleanupFailed, MismatchCleanupFailed };
// Code compare-exchange: replaces the n bytes at site with `desired` only if they still equal
// `expect`, with one locked 8-byte compare-exchange of the Rsxr_CasWindow window (a thread running
// the code sees the old or the new bytes, never a mix). Mismatch: the bytes are not `expect`, nothing
// written. Bytes of the window outside the site may change concurrently; the exchange then retries.
// WrittenCleanupFailed / MismatchCleanupFailed retain the exchange outcome when protection restoration fails.
RsxrCasResult Rsxr_CasCode(uintptr_t site, const uint8_t* expect, const uint8_t* desired, size_t n);
