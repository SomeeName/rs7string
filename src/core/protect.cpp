#include "core/protect.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
DWORD g_ssn = 0;
bool g_useSyscall = false;

std::vector<uint8_t> ReadSystemNtdll() {
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process

    lstrcpyW(path + n, L"\\ntdll.dll");
    const HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return {};
    }

    const DWORD size = GetFileSize(f, nullptr);
    std::vector<uint8_t> buf(size);
    DWORD got = 0;
    const BOOL ok = ReadFile(f, buf.data(), size, &got, nullptr);

    CloseHandle(f);
    if (!ok || got != size) {
        return {};
    }

    return buf;
}

// File offset of an RVA in a raw PE file, or -1.
long RvaToOffset(const std::vector<uint8_t>& img, DWORD rva) {
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(img.data());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(img.data() + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + sec[i].SizeOfRawData) {
            return long(rva - sec[i].VirtualAddress + sec[i].PointerToRawData);
        }
    }

    return -1;
}

// `mov eax, imm32` at the start of the export = the syscall number.
DWORD FindSyscallNumber(const std::vector<uint8_t>& img, const char* name) {
    if (img.size() < 0x400) {
        return 0;
    }

    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(img.data());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(img.data() + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const long expOff = RvaToOffset(img, dir.VirtualAddress);
    if (expOff < 0) {
        return 0;
    }

    const auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(img.data() + expOff);
    const long namesOff = RvaToOffset(img, exp->AddressOfNames);
    const long ordsOff = RvaToOffset(img, exp->AddressOfNameOrdinals);
    const long funcsOff = RvaToOffset(img, exp->AddressOfFunctions);
    if (namesOff < 0 || ordsOff < 0 || funcsOff < 0) {
        return 0;
    }

    const auto names = reinterpret_cast<const DWORD*>(img.data() + namesOff);
    const auto ords = reinterpret_cast<const WORD*>(img.data() + ordsOff);
    const auto funcs = reinterpret_cast<const DWORD*>(img.data() + funcsOff);

    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const long nameOff = RvaToOffset(img, names[i]);
        if (nameOff < 0 || std::strcmp(reinterpret_cast<const char*>(img.data() + nameOff), name) != 0) {
            continue;
        }

        const long codeOff = RvaToOffset(img, funcs[ords[i]]);
        if (codeOff < 0 || img[codeOff] != 0xB8) {
            return 0;
        }

        DWORD ssn;

        std::memcpy(&ssn, img.data() + codeOff + 1, 4);
        return ssn;
    }

    return 0;
}

BOOL CALLBACK Init(PINIT_ONCE, PVOID, PVOID*) {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const bool wine = ntdll && GetProcAddress(ntdll, "wine_get_version");
    DWORD wow64Transition = 0;

    __asm {
        mov eax, dword ptr fs:[0C0h]
        mov wow64Transition, eax
    }

    if (!wine && wow64Transition) {
        g_ssn = FindSyscallNumber(ReadSystemNtdll(), "NtProtectVirtualMemory");
        g_useSyscall = g_ssn != 0;
    }

    return TRUE;
}

// Same stack layout as the 32-bit ntdll stub: the WOW64 transition finds the arguments above
// the return address pushed by `call edx`.
__declspec(naked) LONG NTAPI SyscallProtect(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG) {
    __asm {
        mov eax, g_ssn
        mov edx, dword ptr fs:[0C0h]
        call edx
        ret 14h
    }
}
}  // namespace

static volatile uintptr_t g_failLo = 0, g_failHi = 0;  // tests: injected failures (review #8)

void Rs7_ProtectInjectFailure(uintptr_t lo, uintptr_t hi) {
    g_failLo = lo;
    g_failHi = hi;
}

bool Rs7_Protect(void* addr, size_t len, DWORD newProt, DWORD* oldProt) {
    InitOnceExecuteOnce(&g_once, Init, nullptr, nullptr);

    if (g_failHi) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
        if (a < g_failHi && a + len > g_failLo) {
            return false;
        }
    }

    if (g_useSyscall) {
        PVOID base = addr;
        SIZE_T size = len;
        ULONG old = 0;
        if (SyscallProtect(GetCurrentProcess(), &base, &size, newProt, &old) < 0) {
            return false;
        }

        if (oldProt) {
            *oldProt = old;
        }
        return true;
    }

    DWORD old = 0;
    if (!VirtualProtect(addr, len, newProt, &old)) {
        return false;
    }

    if (oldProt) {
        *oldProt = old;
    }

    return true;
}

const char* Rs7_ProtectMethod() {
    InitOnceExecuteOnce(&g_once, Init, nullptr, nullptr);

    return g_useSyscall ? "syscall" : "VirtualProtect";
}
