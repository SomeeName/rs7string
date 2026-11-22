#pragma once
#include <windows.h>
#include <cstdint>

// Changes page protection without the ntdll export that VMProtect hooks in the game process:
// a direct WOW64 syscall whose number is read from ntdll.dll on disk (SysWOW64). Falls back to
// VirtualProtect under Wine or when the syscall number cannot be determined.
bool Rsxr_Protect(void* addr, size_t len, DWORD newProt, DWORD* oldProt);
// "syscall" or "VirtualProtect", for the log.
const char* Rsxr_ProtectMethod();
// Tests: calls touching [lo, hi) fail until reset with (0, 0); protection != 0 restricts the new protection.
void Rsxr_ProtectInjectFailure(uintptr_t lo, uintptr_t hi, DWORD protection = 0);
