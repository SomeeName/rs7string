#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace rsxr {
// The twelve-slot resource IFile ABI. Storage and destruction belong to the caller: a borrowed
// stack stream must never be freed by the game; an owned stream may outlive its loading call.
struct MemoryFile {
    const void* const* vt = nullptr;
    volatile long refs = 1;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t pos = 0;
};

struct MemoryFileOps {
    static long __fastcall AddRef(MemoryFile* f, void*) { return _InterlockedIncrement(&f->refs); }

    static long DropRef(MemoryFile* f) { return _InterlockedDecrement(&f->refs); }

    template <bool AdvanceNull>
    static uint32_t __fastcall Read(MemoryFile* f, void*, void* dst, uint32_t n) {
        const uint32_t left = f->size - f->pos;

        if (n > left) {
            n = left;
        }
        if (n && !dst && !AdvanceNull) {
            return 0;
        }

        if (n && dst) {
            std::memcpy(dst, f->data + f->pos, n);
        }
        f->pos += n;

        return n;
    }

    static uint32_t __fastcall Size(MemoryFile* f, void*) { return f->size; }

    static uint32_t __fastcall Tell(MemoryFile* f, void*) { return f->pos; }

    static bool __fastcall Seek(MemoryFile* f, void*, uint32_t pos) {
        if (pos > f->size) {
            return false;
        }

        f->pos = pos;

        return true;
    }

    static uint64_t __fastcall Size64(MemoryFile* f, void*) { return f->size; }

    static bool __fastcall True(MemoryFile*, void*) { return true; }

    template <bool RequireData>
    static bool __fastcall Good(MemoryFile* f, void*) {
        return !RequireData || f->data != nullptr;
    }
};

template <bool AdvanceNull, bool RequireData>
std::array<const void*, 12> MemoryFileVtable(void(__fastcall* destroy)(MemoryFile*, void*, uint32_t),
                                             long(__fastcall* release)(MemoryFile*, void*)) {
    using Ops = MemoryFileOps;

    return {reinterpret_cast<const void*>(destroy),    reinterpret_cast<const void*>(&Ops::AddRef),
            reinterpret_cast<const void*>(release),    reinterpret_cast<const void*>(&Ops::Read<AdvanceNull>),
            reinterpret_cast<const void*>(&Ops::Size), reinterpret_cast<const void*>(&Ops::Tell),
            reinterpret_cast<const void*>(&Ops::Seek), reinterpret_cast<const void*>(&Ops::Size64),
            reinterpret_cast<const void*>(&Ops::True), reinterpret_cast<const void*>(&Ops::Good<RequireData>),
            reinterpret_cast<const void*>(&Ops::True), reinterpret_cast<const void*>(&Ops::True)};
}
}  // namespace rsxr
