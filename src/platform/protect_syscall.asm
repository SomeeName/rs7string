; The direct NtProtectVirtualMemory syscall of protect.cpp (Rs7_Protect). Same stack layout as the 32-bit ntdll stub:
; the WOW64 transition (fs:[0C0h]) finds the five stdcall arguments above the return address pushed by `call edx`.

        .686
        .model flat

        EXTERN  _g_rs7_protect_ssn:DWORD        ; the syscall number, read from ntdll.dll on disk

        .code

; LONG NTAPI Rs7_SyscallProtect(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG): stdcall, 20 bytes of arguments.
_Rs7_SyscallProtect@20 PROC
        mov     eax, _g_rs7_protect_ssn
        ASSUME  fs:NOTHING
        mov     edx, dword ptr fs:[0C0h]        ; Wow64Transition
        call    edx
        ret     14h
_Rs7_SyscallProtect@20 ENDP

        END
