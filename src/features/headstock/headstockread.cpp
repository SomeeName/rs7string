#include "features/headstock/headstockread.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdint>
#include <vector>

namespace {
// September 2022 only: fixed headstock templates read through the game VFS.
constexpr uint32_t kChecksum = 0x00B13D7C;
constexpr uintptr_t kFsFrontend = 0x13657C4;  // *: VFS frontend, vtbl +0x10 = Open(path, &file)
constexpr uintptr_t kPsarcCreate = 0x8A33A0;  // ECX = stream, 6 cdecl args, returns archive

using Open_t = bool(__thiscall*)(void* self, const char* path, void** out);
using Ref_t = long(__thiscall*)(void* self);
using OpenEntry_t = bool(__thiscall*)(void* archive, const char* name, void** out);
using Read_t = int(__thiscall*)(void* file, void* buf, int size);
using GetSize_t = uint32_t(__thiscall*)(void* file);

void** Vt(void* o) {
    return *static_cast<void***>(o);
}

void AddRef(void* o) {
    reinterpret_cast<Ref_t>(Vt(o)[1])(o);
}

void Release(void* o) {
    reinterpret_cast<Ref_t>(Vt(o)[2])(o);
}

// Psarc_CreateFromStream(ECX = stream, id 0, flags 1 (keep names), 0, 0, 0, names NULL).
__declspec(naked) void* __stdcall CallPsarcCreate(void* /*stream*/) {
    __asm {
        push ebp
        mov ebp, esp
        push 0
        push 0
        push 0
        push 0
        push 1
        push 0
        mov ecx, [ebp + 8]
        mov eax, kPsarcCreate
        call eax
        add esp, 0x18
        pop ebp
        ret 4
    }
}

int SehFilter(const char* what, const char* name) {
    Rs7Log("headstock: exception in %s (%s) - skipped", what, name ? name : "?");

    return EXCEPTION_EXECUTE_HANDLER;
}

bool SafeOpen(void* fe, const char* path, void** out) {
    __try {
        return reinterpret_cast<Open_t>(Vt(fe)[4])(fe, path, out);
    } __except (SehFilter("open", path)) {
        return false;
    }
}

void* SafeCreate(void* stream, const char* path) {
    __try {
        return CallPsarcCreate(stream);
    } __except (SehFilter("archive", path)) {
        return nullptr;
    }
}

bool SafeOpenEntry(void* archive, const char* name, void** out) {
    __try {
        return reinterpret_cast<OpenEntry_t>(Vt(archive)[4])(archive, name, out);
    } __except (SehFilter("entry", name)) {
        return false;
    }
}

int SafeRead(void* file, void* buf, int size, const char* name) {
    __try {
        return reinterpret_cast<Read_t>(Vt(file)[3])(file, buf, size);
    } __except (SehFilter("read", name)) {
        return -1;
    }
}

uint32_t SafeSize(void* file) {
    __try {
        return reinterpret_cast<GetSize_t>(Vt(file)[4])(file);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// SEH kept separate from vector-owning functions (MSVC C2712).
void* HeadstockFrontend() {
    __try {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return nullptr;
        }

        const auto nt =
            reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.CheckSum != kChecksum) {
            return nullptr;
        }

        return *reinterpret_cast<void**>(kFsFrontend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ReadHeadstockEntry(void* archive, const char* name, std::vector<uint8_t>& bytes) {
    void* file = nullptr;
    if (!SafeOpenEntry(archive, name, &file) || !file) {
        return false;
    }

    AddRef(file);
    const auto size = SafeSize(file);
    bool ok = size > 0 && size <= (64u << 20);

    try {
        if (ok) {
            bytes.resize(size);
            size_t got = 0;

            while (got < size) {
                const int n = SafeRead(file, bytes.data() + got, int(size - got), name);

                if (n <= 0 || uint32_t(n) > size - got) {
                    ok = false;
                    break;
                }

                got += uint32_t(n);
            }
        }
    } catch (...) {
        ok = false;
    }

    Release(file);
    if (!ok) {
        bytes.clear();
    }

    return ok;
}

}  // namespace

namespace {
bool ReadTemplatePair(const char* psarc, const char* guitarEntry, std::vector<uint8_t>& guitar,
                      std::vector<uint8_t>& peg) {
    guitar.clear();
    peg.clear();
    void* fe = HeadstockFrontend();
    if (!fe) {
        return false;
    }

    void* stream = nullptr;
    if (!SafeOpen(fe, psarc, &stream) || !stream) {
        return false;
    }

    AddRef(stream);
    void* archive = SafeCreate(stream, psarc);

    Release(stream);
    if (!archive) {
        return false;
    }

    AddRef(archive);
    const bool ok = ReadHeadstockEntry(archive, guitarEntry, guitar) &&
                    ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/epi_whilshire_peg.nif", peg);

    Release(archive);

    if (!ok) {
        guitar.clear();
        peg.clear();
    }

    return ok;
}
}  // namespace

bool Rs7HeadstockRead_HeadstockTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg) {
    return ReadTemplatePair("nativeexe/base/dependency_guitarstandardinlinedrh.psarc",
                            "assets/gameplay/guitar_standard_inline/epi_wilshire_cherry.nif", guitar, peg);
}

// Gitarrenkopfplatte 3+3 (assetload_load.cpp): the in-line peg and the one texture of it that the 3+3 peg package
// does not carry (wilshire_hardware.dds; hardware_gloss / loft_env_map / normal_sm have 3+3 namesakes).
bool Rs7HeadstockRead_InlinePeg(std::vector<uint8_t>& peg, std::vector<uint8_t>& hardware) {
    peg.clear();
    hardware.clear();
    const char* psarc = "nativeexe/base/dependency_guitarstandardinlinedrh.psarc";
    void* fe = HeadstockFrontend();
    if (!fe) {
        return false;
    }

    void* stream = nullptr;
    if (!SafeOpen(fe, psarc, &stream) || !stream) {
        return false;
    }

    AddRef(stream);
    void* archive = SafeCreate(stream, psarc);

    Release(stream);
    if (!archive) {
        return false;
    }

    AddRef(archive);
    const bool ok =
        ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/epi_whilshire_peg.nif", peg) &&
        ReadHeadstockEntry(archive, "assets/gameplay/guitar_standard_inline/wilshire_hardware.dds", hardware);

    Release(archive);

    if (!ok) {
        peg.clear();
        hardware.clear();
    }

    return ok;
}

// The left-handed in-line frame; its archive carries the same peg (identical bytes, CONFIRMED by SHA-256 on the
// owner's install), so the left-handed package is self-contained.
bool Rs7HeadstockRead_HeadstockTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg) {
    return ReadTemplatePair("nativeexe/base/dependency_guitarstandardinlinedlh.psarc",
                            "assets/gameplay/guitar_standard_inline/epi_whilshire_cherry_lh.nif", guitar, peg);
}
