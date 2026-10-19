; Shared mid-hook thunks (ARCHITECTURE.md section 15). Every generated entry (Rs7Mid_Build, Rs7Mid_BuildLight,
; Rs7Mid_BuildSse, Rs7Mid_BuildCall, Rs7Mid_BuildWrap in midhook.cpp / midhook_wrap.cpp) pushes the continuation and the callback and jumps here, so on entry:
;   [esp+0] callback, [esp+4] continuation (replay copy, callee or site + 5), [esp+8] the site's stack.
; The thunks build an Rs7Mid::Context (midhook.h, 44 bytes, its offsets are static_asserted there), call the cdecl
; callback with a pointer to it, and resume the edited registers, flags and ESP at Context::jump.
;
; Context offsets used below (PUSHAD order, then the thunk-owned slots):
;   +0 edi  +4 esi  +8 ebp  +12 esp  +16 ebx  +20 edx  +24 ecx  +28 eax  +32 eflags  +36 fpu  +40 jump
; The frame written back for POPAD / POPFD / RET is the first nine dwords plus jump, 40 bytes below the requested
; ESP (at least 40 writable bytes below it are part of the contract).

        .686
        .xmm
        .model flat, C

        .code

; Full thunk (Rs7Mid::Mode::Full). The context is a copy of the PUSHAD/PUSHFD frame and the two entry slots, so
; restoring ESP never depends on POPAD's ignored ESP slot. FXSAVE saves x87/MMX/SSE/MXCSR (the game requires an SSE
; CPU), the callback runs after FNINIT with DF = 0 and gets the image in Context::fpu, FXRSTOR restores it (with the
; callback's St0 edits).
Rs7Mid_Thunk PROC EXPORT
        pushfd
        pushad
        mov     esi, esp                ; source: the saved frame and the two entry slots
        sub     esp, 44
        mov     edi, esp                ; destination: the context
        mov     ecx, 11
        cld
        rep movsd
        mov     ebp, esp                ; ebp = &Context
        add     dword ptr [ebp+12], 12  ; Context::esp: the site's ESP, above the entry slots and PUSHFD
        sub     esp, 528
        and     esp, -16
        mov     ebx, esp                ; ebx = 16-byte aligned 512-byte FXSAVE area
        fxsave  [ebx]
        fninit
        mov     eax, [ebp+36]           ; the callback (entry slot copied into Context::fpu)
        mov     [ebp+36], ebx           ; Context::fpu = the FXSAVE image
        push    ebp
        call    eax
        fxrstor [ebx]

        ; Copy the nine register dwords under the requested ESP; the destination may overlap the context, so copy
        ; downwards (STD) when it lies above it.
        cld
        mov     esi, ebp
        mov     edi, [ebp+12]
        sub     edi, 40
        mov     edx, edi                ; edx = the new frame
        mov     eax, [ebp+40]           ; Context::jump, read before the copy can overwrite it
        mov     ecx, 9
        cmp     edi, esi
        jbe     forward_copy
        add     esi, 32
        add     edi, 32
        std
forward_copy:
        rep movsd
        cld
        mov     [edx+36], eax           ; the RET slot
        mov     esp, edx
        popad
        popfd
        ret
Rs7Mid_Thunk ENDP

; Copy the nine register dwords of the context at ebp to the frame at edi, one dword per pair of moves, at the given
; offsets (in order). Clobbers ecx.
COPY_FRAME MACRO offsets:VARARG
        FOR     off, <offsets>
        mov     ecx, [ebp+off]
        mov     [edi+off], ecx
        ENDM
ENDM

; The light and SSE thunks' resume (ebp = &Context): copy the nine register dwords under the requested ESP (upwards or
; downwards, as the frames overlap), put Context::jump in the RET slot, then POPAD / POPFD / RET.
RESUME_FRAME MACRO
        LOCAL   copy_down, copied
        mov     edi, [ebp+12]
        sub     edi, 40                 ; edi = the new frame
        mov     eax, [ebp+40]           ; Context::jump, read before the copy can overwrite it
        cmp     edi, ebp
        ja      copy_down
        COPY_FRAME 0, 4, 8, 12, 16, 20, 24, 28, 32
        jmp     copied
copy_down:
        COPY_FRAME 32, 28, 24, 20, 16, 12, 8, 4, 0
copied:
        mov     [edi+36], eax           ; the RET slot
        mov     esp, edi
        popad
        popfd
        ret
ENDM

; Light thunk (Rs7Mid::Mode::Light): the same Context and continuation without any floating-point save and with
; unrolled copies instead of REP MOVSD / STD. The callback must not touch x87/MMX/SSE state (midhook.h). The context
; lies 44 bytes below the entry slots, as in the full thunk, so a callback may push up to eleven dwords below the
; site ESP (Rs7Mid::Push) without overwriting it.
Rs7Mid_ThunkLight PROC EXPORT
        lea     esp, [esp-44]           ; keeps the flags (no SUB)
        pushfd
        pushad
        mov     ebp, esp                ; ebp = &Context
        mov     dword ptr [ebp+36], 0   ; Context::fpu: no FXSAVE image
        mov     eax, [ebp+84]           ; the continuation entry slot
        mov     [ebp+40], eax           ; Context::jump
        add     dword ptr [ebp+12], 56  ; Context::esp: the site's ESP, above the gap, entry slots and PUSHFD
        cld
        push    ebp
        call    dword ptr [ebp+80]      ; the callback entry slot
        RESUME_FRAME
Rs7Mid_ThunkLight ENDP

; SSE thunk (Rs7Mid::Mode::Sse): the light thunk plus XMM0-7 and MXCSR, for hot callbacks that compute with SSE. No
; FXSAVE, no FNINIT: the game's x87 state is untouched, so the callback must not execute x87 or MMX code (midhook.h,
; SSE_CALLBACKS). MXCSR is the default 1F80h (round to nearest, all exceptions masked, no FTZ/DAZ) while it runs.
; Context::fpu = the address of the result block (Rs7Mid::SseResult at ebp+44) with bit 0 set; when the callback set
; its `load` (Rs7Mid::LoadSt0), the thunk loads `value` with `fld dword` after restoring, as a displaced
; `fld dword [..]` would. The gap is 52 bytes: the context tail and the block stay below the eleven dwords a
; callback may Push under the site ESP.
Rs7Mid_ThunkSse PROC EXPORT
        lea     esp, [esp-52]           ; keeps the flags (no SUB)
        pushfd
        pushad
        mov     ebp, esp                ; ebp = &Context
        lea     eax, [ebp+45]
        mov     [ebp+36], eax           ; Context::fpu: the result block, tagged with bit 0
        mov     dword ptr [ebp+48], 0   ; SseResult::load
        mov     eax, [ebp+92]           ; the continuation entry slot
        mov     [ebp+40], eax           ; Context::jump
        add     dword ptr [ebp+12], 64  ; Context::esp: the site's ESP, above the gap, entry slots and PUSHFD
        sub     esp, 132                ; XMM0-7 and MXCSR (unaligned: MOVUPS)
        movups  [esp], xmm0
        movups  [esp+16], xmm1
        movups  [esp+32], xmm2
        movups  [esp+48], xmm3
        movups  [esp+64], xmm4
        movups  [esp+80], xmm5
        movups  [esp+96], xmm6
        movups  [esp+112], xmm7
        stmxcsr [esp+128]
        push    1F80h
        ldmxcsr [esp]
        mov     [esp], ebp              ; the same slot is the callback's argument
        cld
        call    dword ptr [ebp+88]      ; the callback entry slot
        ldmxcsr [esp+132]
        movups  xmm0, [esp+4]
        movups  xmm1, [esp+20]
        movups  xmm2, [esp+36]
        movups  xmm3, [esp+52]
        movups  xmm4, [esp+68]
        movups  xmm5, [esp+84]
        movups  xmm6, [esp+100]
        movups  xmm7, [esp+116]
        cmp     dword ptr [ebp+48], 0
        je      no_load
        fld     dword ptr [ebp+44]      ; SseResult::value
no_load:
        RESUME_FRAME
Rs7Mid_ThunkSse ENDP

; void __cdecl Rs7Mid_Fldcw(uint32_t fcw): load the low word as the x87 control word (Rs7Mid::GameFcw, midhook.h).
Rs7Mid_Fldcw PROC EXPORT
        fldcw   word ptr [esp+4]
        ret
Rs7Mid_Fldcw ENDP

        END
