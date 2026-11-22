#include "platform/freeze_stack.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <algorithm>

namespace {
bool Refuse(RsxrFrozenRefusal& refusal, RsxrFrozenReason reason, uintptr_t address, uintptr_t value = 0) {
    refusal.reason = reason;
    refusal.address = address;
    refusal.value = value;

    return true;
}

bool ReadableRegion(uintptr_t address, MEMORY_BASIC_INFORMATION* memory, RsxrFrozenRefusal& refusal) {
    refusal.state = refusal.protect = 0;

    if (!VirtualQuery(reinterpret_cast<const void*>(address), memory, sizeof *memory)) {
        refusal.error = GetLastError();
        Refuse(refusal, RsxrFrozenReason::Unreadable, address);
        return false;
    }

    refusal.state = memory->State;
    refusal.protect = memory->Protect;

    // SafeCopy deliberately does not swallow guard-page faults: never touch those pages.
    if (memory->Protect & PAGE_GUARD) {
        Refuse(refusal, RsxrFrozenReason::GuardPage, address);
        return false;
    }

    if (memory->State != MEM_COMMIT || (memory->Protect & PAGE_NOACCESS)) {
        Refuse(refusal, RsxrFrozenReason::Unreadable, address);
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

bool ScanStack(const RsxrFrozenContext& context, const RsxrRange& range, RsxrFrozenRefusal& refusal) {
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
            return Refuse(refusal, RsxrFrozenReason::AllocationBoundary, cursor,
                          reinterpret_cast<uintptr_t>(memory.AllocationBase));
        }
        size_t bytes = std::min(context.stackBase - cursor, Available(cursor, memory));
        if (bytes < sizeof(uintptr_t)) {
            return Refuse(refusal, RsxrFrozenReason::ShortRegion, cursor, bytes);
        }

        while (bytes >= sizeof(uintptr_t)) {
            const size_t count = std::min(bytes / sizeof(uintptr_t), sizeof words / sizeof words[0]);
            const size_t copied = count * sizeof(uintptr_t);
            if (!Rsxr_SafeCopy(words, reinterpret_cast<const void*>(cursor), copied)) {
                return Refuse(refusal, RsxrFrozenReason::CopyFailed, cursor, copied);
            }

            for (size_t i = 0; i < count; ++i) {
                if (words[i] >= range.lo && words[i] < range.hi) {
                    return Refuse(refusal, RsxrFrozenReason::ReturnAddress, cursor + i * sizeof(uintptr_t), words[i]);
                }
            }

            cursor += copied;
            bytes -= copied;
        }
    }

    return false;
}
}  // namespace

RsxrFrozenContext RsxrFrozenContext::Capture(void* thread, uintptr_t eip, uintptr_t esp, unsigned long segFs) {
    // CONTEXT.SegFs is a DWORD, but only its low 16 bits are the selector; the game's threads were seen with
    // garbage above them (0x1A0053), which GetThreadSelectorEntry rejects.
    const unsigned long fs = segFs & 0xFFFF;
    RsxrFrozenContext context{eip, esp};
    RsxrFrozenRefusal& refusal = context.capture;

    refusal.fs = fs;
    LDT_ENTRY entry{};

    if (!fs) {
        Refuse(refusal, RsxrFrozenReason::NoFs, 0);
        return context;
    }

    if (!GetThreadSelectorEntry(static_cast<HANDLE>(thread), fs, &entry)) {
        refusal.error = GetLastError();
        Refuse(refusal, RsxrFrozenReason::SelectorFailure, fs);
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
        Refuse(refusal, RsxrFrozenReason::ShortRegion, teb, Available(teb, memory));
        return context;
    }

    if (!Rsxr_SafeCopy(&tib, reinterpret_cast<const void*>(teb), sizeof tib)) {
        Refuse(refusal, RsxrFrozenReason::CopyFailed, teb, sizeof tib);
        return context;
    }

    context.stackLimit = reinterpret_cast<uintptr_t>(tib.StackLimit);
    context.stackBase = reinterpret_cast<uintptr_t>(tib.StackBase);
    if (!teb || reinterpret_cast<uintptr_t>(tib.Self) != teb) {
        Refuse(refusal, RsxrFrozenReason::TibMismatch, teb + offsetof(NT_TIB, Self),
               reinterpret_cast<uintptr_t>(tib.Self));
    }

    return context;
}

bool RsxrFrozenContext::CallsRange(const RsxrRange& range, RsxrFrozenRefusal* out, unsigned long tid) const {
    RsxrFrozenRefusal refusal = capture;

    refusal.tid = tid;
    refusal.eip = eip;
    refusal.esp = esp;
    refusal.stackLimit = stackLimit;
    refusal.stackBase = stackBase;
    refusal.range = range;
    bool refused = true;

    if (eip >= range.lo && eip < range.hi) {
        Refuse(refusal, RsxrFrozenReason::Eip, eip, eip);
    } else if (capture.reason != RsxrFrozenReason::None) {
        // Capture preserved the exact selector/TIB failure; it still refuses writes.
    } else if (!stackLimit || esp < stackLimit || esp >= stackBase) {
        Refuse(refusal, RsxrFrozenReason::EspOutside, esp);
    } else if (esp % sizeof(uintptr_t) || stackBase % sizeof(uintptr_t)) {
        Refuse(refusal, RsxrFrozenReason::Unaligned, esp, stackBase);
    } else {
        refused = ScanStack(*this, range, refusal);
    }
    if (refused && out) {
        *out = refusal;
    }

    return refused;
}

const char* Rsxr_FrozenReasonName(RsxrFrozenReason reason) {
    switch (reason) {
        case RsxrFrozenReason::None:
            return "none";
        case RsxrFrozenReason::NoFs:
            return "no-fs";
        case RsxrFrozenReason::SelectorFailure:
            return "selector-failure";
        case RsxrFrozenReason::TibMismatch:
            return "tib-self-mismatch";
        case RsxrFrozenReason::EspOutside:
            return "esp-outside-stack";
        case RsxrFrozenReason::Unaligned:
            return "unaligned-stack";
        case RsxrFrozenReason::Unreadable:
            return "unreadable-region";
        case RsxrFrozenReason::GuardPage:
            return "guard-page";
        case RsxrFrozenReason::AllocationBoundary:
            return "allocation-boundary";
        case RsxrFrozenReason::ShortRegion:
            return "short-region";
        case RsxrFrozenReason::CopyFailed:
            return "copy-failed";
        case RsxrFrozenReason::Eip:
            return "eip-in-range";
        case RsxrFrozenReason::ReturnAddress:
            return "return-address-hit";
    }

    return "unknown";
}
