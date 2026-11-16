#include "platform/freeze_stack.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <algorithm>

namespace {
bool Refuse(Rs7FrozenRefusal& refusal, Rs7FrozenReason reason, uintptr_t address, uintptr_t value = 0) {
    refusal.reason = reason;
    refusal.address = address;
    refusal.value = value;

    return true;
}

bool ReadableRegion(uintptr_t address, MEMORY_BASIC_INFORMATION* memory, Rs7FrozenRefusal& refusal) {
    refusal.state = refusal.protect = 0;

    if (!VirtualQuery(reinterpret_cast<const void*>(address), memory, sizeof *memory)) {
        refusal.error = GetLastError();
        Refuse(refusal, Rs7FrozenReason::Unreadable, address);
        return false;
    }

    refusal.state = memory->State;
    refusal.protect = memory->Protect;

    // SafeCopy deliberately does not swallow guard-page faults: never touch those pages.
    if (memory->Protect & PAGE_GUARD) {
        Refuse(refusal, Rs7FrozenReason::GuardPage, address);
        return false;
    }

    if (memory->State != MEM_COMMIT || (memory->Protect & PAGE_NOACCESS)) {
        Refuse(refusal, Rs7FrozenReason::Unreadable, address);
        return false;
    }

    return true;
}

size_t Available(uintptr_t address, const MEMORY_BASIC_INFORMATION& memory) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    if (address < base || address - base >= memory.RegionSize) {
        return 0;
    }

    return memory.RegionSize - (address - base);
}

bool ScanStack(const Rs7FrozenContext& context, const Rs7Range& range, Rs7FrozenRefusal& refusal) {
    uintptr_t cursor = context.esp;
    void* allocation = nullptr;
    uintptr_t words[64];

    while (cursor < context.stackBase) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!ReadableRegion(cursor, &memory, refusal)) {
            return true;
        }

        if (!allocation) {
            allocation = memory.AllocationBase;
        } else if (memory.AllocationBase != allocation) {
            return Refuse(refusal, Rs7FrozenReason::AllocationBoundary, cursor,
                          reinterpret_cast<uintptr_t>(memory.AllocationBase));
        }
        size_t bytes = std::min(context.stackBase - cursor, Available(cursor, memory));
        if (bytes < sizeof(uintptr_t)) {
            return Refuse(refusal, Rs7FrozenReason::ShortRegion, cursor, bytes);
        }

        while (bytes >= sizeof(uintptr_t)) {
            const size_t count = std::min(bytes / sizeof(uintptr_t), sizeof words / sizeof words[0]);
            const size_t copied = count * sizeof(uintptr_t);
            if (!Rs7_SafeCopy(words, reinterpret_cast<const void*>(cursor), copied)) {
                return Refuse(refusal, Rs7FrozenReason::CopyFailed, cursor, copied);
            }

            for (size_t i = 0; i < count; ++i) {
                if (words[i] >= range.lo && words[i] < range.hi) {
                    return Refuse(refusal, Rs7FrozenReason::ReturnAddress, cursor + i * sizeof(uintptr_t), words[i]);
                }
            }

            cursor += copied;
            bytes -= copied;
        }
    }

    return false;
}
}  // namespace

Rs7FrozenContext Rs7FrozenContext::Capture(void* thread, uintptr_t eip, uintptr_t esp, unsigned long segFs) {
    // CONTEXT.SegFs is a DWORD, but only its low 16 bits are the selector; the game's threads were seen with
    // garbage above them (0x1A0053), which GetThreadSelectorEntry rejects.
    const unsigned long fs = segFs & 0xFFFF;
    Rs7FrozenContext context{eip, esp};
    Rs7FrozenRefusal& refusal = context.capture;

    refusal.fs = fs;
    LDT_ENTRY entry{};

    if (!fs) {
        Refuse(refusal, Rs7FrozenReason::NoFs, 0);
        return context;
    }

    if (!GetThreadSelectorEntry(static_cast<HANDLE>(thread), fs, &entry)) {
        refusal.error = GetLastError();
        Refuse(refusal, Rs7FrozenReason::SelectorFailure, fs);
        return context;
    }

    const uintptr_t teb = uintptr_t(entry.BaseLow) | (uintptr_t(entry.HighWord.Bytes.BaseMid) << 16) |
                          (uintptr_t(entry.HighWord.Bytes.BaseHi) << 24);

    refusal.teb = teb;
    MEMORY_BASIC_INFORMATION memory{};
    NT_TIB tib{};
    if (!ReadableRegion(teb, &memory, refusal)) {
        return context;
    }

    if (Available(teb, memory) < sizeof tib) {
        Refuse(refusal, Rs7FrozenReason::ShortRegion, teb, Available(teb, memory));
        return context;
    }

    if (!Rs7_SafeCopy(&tib, reinterpret_cast<const void*>(teb), sizeof tib)) {
        Refuse(refusal, Rs7FrozenReason::CopyFailed, teb, sizeof tib);
        return context;
    }

    context.stackLimit = reinterpret_cast<uintptr_t>(tib.StackLimit);
    context.stackBase = reinterpret_cast<uintptr_t>(tib.StackBase);
    if (!teb || reinterpret_cast<uintptr_t>(tib.Self) != teb) {
        Refuse(refusal, Rs7FrozenReason::TibMismatch, teb + offsetof(NT_TIB, Self),
               reinterpret_cast<uintptr_t>(tib.Self));
    }

    return context;
}

bool Rs7FrozenContext::CallsRange(const Rs7Range& range, Rs7FrozenRefusal* out, unsigned long tid) const {
    Rs7FrozenRefusal refusal = capture;

    refusal.tid = tid;
    refusal.eip = eip;
    refusal.esp = esp;
    refusal.stackLimit = stackLimit;
    refusal.stackBase = stackBase;
    refusal.range = range;
    bool refused = true;

    if (eip >= range.lo && eip < range.hi) {
        Refuse(refusal, Rs7FrozenReason::Eip, eip, eip);
    } else if (capture.reason != Rs7FrozenReason::None) {
        // Capture preserved the exact selector/TIB failure; it still refuses writes.
    } else if (!stackLimit || esp < stackLimit || esp >= stackBase) {
        Refuse(refusal, Rs7FrozenReason::EspOutside, esp);
    } else if (esp % sizeof(uintptr_t) || stackBase % sizeof(uintptr_t)) {
        Refuse(refusal, Rs7FrozenReason::Unaligned, esp, stackBase);
    } else {
        refused = ScanStack(*this, range, refusal);
    }
    if (refused && out) {
        *out = refusal;
    }

    return refused;
}

const char* Rs7_FrozenReasonName(Rs7FrozenReason reason) {
    switch (reason) {
        case Rs7FrozenReason::None:
            return "none";
        case Rs7FrozenReason::NoFs:
            return "no-fs";
        case Rs7FrozenReason::SelectorFailure:
            return "selector-failure";
        case Rs7FrozenReason::TibMismatch:
            return "tib-self-mismatch";
        case Rs7FrozenReason::EspOutside:
            return "esp-outside-stack";
        case Rs7FrozenReason::Unaligned:
            return "unaligned-stack";
        case Rs7FrozenReason::Unreadable:
            return "unreadable-region";
        case Rs7FrozenReason::GuardPage:
            return "guard-page";
        case Rs7FrozenReason::AllocationBoundary:
            return "allocation-boundary";
        case Rs7FrozenReason::ShortRegion:
            return "short-region";
        case Rs7FrozenReason::CopyFailed:
            return "copy-failed";
        case Rs7FrozenReason::Eip:
            return "eip-in-range";
        case Rs7FrozenReason::ReturnAddress:
            return "return-address-hit";
    }

    return "unknown";
}
