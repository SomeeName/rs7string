#pragma once
#include <cstddef>
#include <cstdint>

// Every writer makes the bytes writable, writes, restores the protection and flushes the
// instruction cache. All return false without writing on a precondition failure.
bool Rs7_WriteBytes(uintptr_t at, const void* bytes, size_t len);
// Review #8: writes `original` back and reports success only if a read-back compare matches. Heap-free
// (safe inside a thread freeze).
bool Rs7_RestoreBytes(uintptr_t at, const void* original, size_t len);
// E9 rel32 to dest, NOP-filled to `length` (5..16).
bool Rs7_WriteJmp(uintptr_t site, size_t length, uintptr_t dest);
// 5-byte jmp that must lie inside one aligned qword; written with one locked 8-byte exchange.
bool Rs7_WriteJmpAtomic8(uintptr_t site, uintptr_t dest);
// Retarget a rel32 operand at relAt of an instruction ending at insnEnd. atomic: one aligned
// 32-bit store (relAt must be 4-aligned).
bool Rs7_WriteRel32(uintptr_t relAt, uintptr_t insnEnd, uintptr_t dest, bool atomic);
// Aligned 4-byte pointer store into (possibly read-only) data, e.g. a vtable slot: one interlocked
// exchange, protection restored. An executable page stays executable while it is writable. *old (if
// non-null) gets the previous value.
bool Rs7_WritePtrAtomic(uintptr_t at, uintptr_t value, uintptr_t* old);

// An 8-byte window holding [site, site + n) (n 1..8) that lies inside one 64-byte cache line: the
// aligned qword when it fits, else the lowest such start. False if none exists (the bytes straddle a
// cache line). A locked 8-byte access inside one cache line is atomic on P6 and later (Intel SDM
// vol. 3A "Guaranteed Atomic Operations"), aligned or not.
bool Rs7_CasWindow(uintptr_t site, size_t n, uintptr_t* window);
enum class Rs7CasResult { Written, Mismatch, Unsupported, ProtectFailed };
// Code compare-exchange: replaces the n bytes at site with `desired` only if they still equal
// `expect`, with one locked 8-byte compare-exchange of the Rs7_CasWindow window (a thread running
// the code sees the old or the new bytes, never a mix). Mismatch: the bytes are not `expect`, nothing
// written. Bytes of the window outside the site may change concurrently; the exchange then retries.
Rs7CasResult Rs7_CasCode(uintptr_t site, const uint8_t* expect, const uint8_t* desired, size_t n);
