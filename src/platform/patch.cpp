#include "platform/patch.h"
#include "platform/protect.h"
#include <windows.h>
#include <cstring>

namespace {
struct Writable {
    void* p;
    size_t n;
    DWORD old = 0;
    bool ok;

    Writable(void* p_, size_t n_, DWORD protection = PAGE_EXECUTE_READWRITE) : p(p_), n(n_) {
        ok = Rsxr_Protect(p, n, protection, &old);
    }

    RsxrWriteResult Finish(bool written) {
        DWORD dummy;
        const bool restored = Rsxr_Protect(p, n, old, &dummy);

        FlushInstructionCache(GetCurrentProcess(), p, n);

        return {written, restored};
    }
};

RsxrCasResult FinishCas(Writable& w, bool written) {
    const RsxrWriteResult result = w.Finish(written);
    if (!result.protectionRestored) {
        return written ? RsxrCasResult::WrittenCleanupFailed : RsxrCasResult::MismatchCleanupFailed;
    }

    return written ? RsxrCasResult::Written : RsxrCasResult::Mismatch;
}

int32_t Rel(uintptr_t insnEnd, uintptr_t dest) {
    return static_cast<int32_t>(dest - insnEnd);
}
}  // namespace

RsxrWriteResult Rsxr_WriteBytes(uintptr_t at, const void* bytes, size_t len) {
    Writable w(reinterpret_cast<void*>(at), len);
    if (!w.ok) {
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(at), bytes, len);

    return w.Finish(true);
}

bool Rsxr_RestoreBytes(uintptr_t at, const void* original, size_t len) {
    const RsxrWriteResult result = Rsxr_WriteBytes(at, original, len);

    return result.protectionRestored && std::memcmp(reinterpret_cast<const void*>(at), original, len) == 0;
}

RsxrWriteResult Rsxr_WriteJmp(uintptr_t site, size_t length, uintptr_t dest) {
    if (length < 5 || length > 16) {
        return false;
    }

    uint8_t buf[16];

    std::memset(buf, 0x90, sizeof buf);
    buf[0] = 0xE9;
    const int32_t rel = Rel(site + 5, dest);

    std::memcpy(buf + 1, &rel, 4);

    return Rsxr_WriteBytes(site, buf, length);
}

RsxrWriteResult Rsxr_WriteJmpAtomic8(uintptr_t site, uintptr_t dest) {
    const uintptr_t q = site & ~uintptr_t(7);
    const size_t off = site - q;
    if (off + 5 > 8) {
        return false;
    }

    Writable w(reinterpret_cast<void*>(q), 8);
    if (!w.ok) {
        return false;
    }

    volatile LONG64* p = reinterpret_cast<volatile LONG64*>(q);
    const LONG64 oldv = *p;
    LONG64 newv = oldv;
    uint8_t* b = reinterpret_cast<uint8_t*>(&newv);

    b[off] = 0xE9;
    const int32_t rel = Rel(site + 5, dest);

    std::memcpy(b + off + 1, &rel, 4);

    return w.Finish(InterlockedCompareExchange64(p, newv, oldv) == oldv);
}

RsxrWriteResult Rsxr_WriteRel32(uintptr_t relAt, uintptr_t insnEnd, uintptr_t dest, bool atomic) {
    const int32_t rel = Rel(insnEnd, dest);
    if (!atomic) {
        return Rsxr_WriteBytes(relAt, &rel, 4);
    }
    if (relAt % 4 != 0) {
        return false;
    }

    Writable w(reinterpret_cast<void*>(relAt), 4);
    if (!w.ok) {
        return false;
    }

    InterlockedExchange(reinterpret_cast<volatile LONG*>(relAt), rel);

    return w.Finish(true);
}

RsxrWriteResult Rsxr_WritePtrAtomic(uintptr_t at, uintptr_t value, uintptr_t* old) {
    if (at % 4 != 0) {
        return false;
    }

    void* const p = reinterpret_cast<void*>(at);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) {
        return false;
    }

    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    Writable w(p, 4, (mbi.Protect & exec) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    if (!w.ok) {
        return false;
    }

    const LONG was = InterlockedExchange(reinterpret_cast<volatile LONG*>(at), static_cast<LONG>(value));

    if (old) {
        *old = static_cast<uintptr_t>(static_cast<ULONG>(was));
    }

    return w.Finish(true);
}

bool Rsxr_WritePtrPair(uintptr_t at, uintptr_t first, uintptr_t second, unsigned* failedSlot) {
    uintptr_t oldFirst = 0, oldSecond = 0;
    const RsxrWriteResult low = Rsxr_WritePtrAtomic(at, first, &oldFirst);

    if (failedSlot) {
        *failedSlot = 0;
    }

    if (low) {
        const RsxrWriteResult high = Rsxr_WritePtrAtomic(at + sizeof(uintptr_t), second, &oldSecond);
        if (high) {
            return true;
        }

        if (failedSlot) {
            *failedSlot = 1;
        }
        if (high.written) {
            Rsxr_WritePtrAtomic(at + sizeof(uintptr_t), oldSecond, nullptr);
        }
    }

    if (low.written) {
        Rsxr_WritePtrAtomic(at, oldFirst, nullptr);
    }

    return false;
}

bool Rsxr_CasWindow(uintptr_t site, size_t n, uintptr_t* window) {
    if (n == 0 || n > 8) {
        return false;
    }

    const uintptr_t q = site & ~uintptr_t(7);

    if (site + n <= q + 8) {
        *window = q;
        return true;
    }

    for (uintptr_t w = site + n - 8; w <= site; ++w) {
        if ((w >> 6) == ((w + 7) >> 6)) {
            *window = w;
            return true;
        }
    }

    return false;
}

RsxrCasResult Rsxr_CasCode(uintptr_t site, const uint8_t* expect, const uint8_t* desired, size_t n) {
    uintptr_t win = 0;
    if (!Rsxr_CasWindow(site, n, &win)) {
        return RsxrCasResult::Unsupported;
    }

    Writable w(reinterpret_cast<void*>(win), 8);
    if (!w.ok) {
        return RsxrCasResult::ProtectFailed;
    }

    volatile LONG64* p = reinterpret_cast<volatile LONG64*>(win);
    const size_t off = site - win;

    for (int attempt = 0; attempt < 16; ++attempt) {
        const LONG64 oldv = InterlockedCompareExchange64(p, 0, 0);  // a consistent snapshot (writes only 0 over 0)
        const uint8_t* ob = reinterpret_cast<const uint8_t*>(&oldv);
        if (std::memcmp(ob + off, expect, n) != 0) {
            return FinishCas(w, false);
        }

        LONG64 newv = oldv;

        std::memcpy(reinterpret_cast<uint8_t*>(&newv) + off, desired, n);
        if (InterlockedCompareExchange64(p, newv, oldv) == oldv) {
            return FinishCas(w, true);
        }
    }

    return FinishCas(w, false);  // the window never held still: treat as changed under us
}
