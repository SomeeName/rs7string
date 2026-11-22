#include "d3d/sehread.h"
#include "platform/seh.h"
#include <cstring>

bool RsxrSehReader::Read(uintptr_t a, void* out, size_t n) const {
    if (a < 0x10000 || a + n < a) {
        return false;
    }

    __try {
        std::memcpy(out, reinterpret_cast<const void*>(a), n);
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

bool Rsxr_ImageRange(HMODULE mod, uintptr_t* lo, uintptr_t* hi) {
    if (!mod) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
    const RsxrSehReader mem{};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!mem.Read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    if (!mem.Read(base + uint32_t(dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    *lo = base;
    *hi = base + nt.OptionalHeader.SizeOfImage;

    return true;
}
