#pragma once
#include "platform/freeze.h"

// Saved only after GetThreadContext confirms the thread is held.
struct RsxrFrozenContext {
    uintptr_t eip, esp;
    uintptr_t stackLimit = 0, stackBase = 0;
    RsxrFrozenRefusal capture{};

    // Capture the real stack bounds from the held thread's FS descriptor and NT_TIB.
    static RsxrFrozenContext Capture(void* thread, uintptr_t eip, uintptr_t esp, unsigned long fs);

    // Conservative call-chain check: EIP or a possible return address from ESP to StackBase.
    // Unknown bounds, an ESP outside them or unreadable stack memory refuse the check.
    bool CallsRange(const RsxrRange& range, RsxrFrozenRefusal* refusal = nullptr, unsigned long tid = 0) const;
};
