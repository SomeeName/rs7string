// Shared conventions of the hook stub layer (src/hooks/stubs_*.cpp).
//
// A "stub" is a naked function the patcher jumps to from a game address (a `jmp` site of sites.json).
// It replays the instructions the jump displaced, does its work, and jumps back to the game. Every
// stub is exported as `Rs7_Stub_<ID>` (the site id of sites.json; tools/tests/test_emu*.py find them
// by that name, so the names never change) together with its link variables:
//
//   g_rs7_resume_<ID>  address after the displaced instructions (site + length), filled in by the
//                      patcher; the stub ends with `jmp g_rs7_resume_<ID>`
//   g_rs7_target_<ID>  an original branch/call target of the displaced code (the `jae` destination,
//                      the callee of a replaced call), when the stub needs it
//
// Reading a stub: the comment above it says WHERE (site id, address, enclosing game function), WHAT the
// stock code does there and WHAT we change; registers named in the comment hold the stock values at the
// site. Rules every stub keeps (docs/DEVELOPMENT.md, AGENTS.md "Inline assembly pitfalls"):
//   - preserve every register and flag the stock code reads afterwards (flags are only free where the
//     comment says "flags dead"; the next flag reader follows a flag-writing instruction);
//   - leave the x87 / SSE state alone unless the comment says it is saved (calls into C++ from an x87-live
//     site use only integer helpers, or the RS7_SAVE_ALL frame);
//   - name inline-asm operands never like a register or mnemonic (`push gs` pushes the GS register; MSVC
//     C4405 reports this class of bug and is an error in this project);
//   - no new hand-written stub where a mid-hook (midhook.h) does the job.
//
// The macros below keep the repeated shapes in one place. They expand to the same instructions the
// hand-written stubs had (checked byte for byte against the earlier build).
#pragma once
#include <cstdint>

// ---- declarations -------------------------------------------------------------------------------

// An exported variable (a link variable above, or a switch read by a stub), zero-initialised.
#define RS7_VAR(name) extern "C" __declspec(dllexport) uintptr_t name = 0
// An exported naked function; the body is `{ __asm { ... } }`.
#define RS7_STUB(name) extern "C" __declspec(dllexport) __declspec(naked) void name()
// The link variables of site <ID> by id (instead of the full name).
#define RS7_RESUME_VAR(id) RS7_VAR(g_rs7_resume_##id)
#define RS7_TARGET_VAR(id) RS7_VAR(g_rs7_target_##id)

// ---- full state save around a call into C++ ------------------------------------------------------

// RS7_SAVE_ALL saves EFLAGS and all GPRs, aligns the stack and saves + resets the x87/SSE state
// (fxsave, fninit), so the C++ callee may use anything. ESI = the pushad frame afterwards (clobbered
// ESI is restored by RS7_RESTORE_ALL). In that frame: [esi+0] EDI, +4 ESI, +8 EBP, +0Ch ESP, +10h EBX,
// +14h EDX, +18h ECX, +1Ch EAX, +20h EFLAGS, +24h the site's [esp] (its return address or stack top).
#define RS7_SAVE_ALL                                                                                                   \
    __asm pushfd __asm pushad __asm mov esi, esp __asm sub esp, 512 __asm and esp, -16 __asm fxsave[esp] __asm fninit
#define RS7_RESTORE_ALL __asm fxrstor[esp] __asm mov esp, esi __asm popad __asm popfd

// ---- entry dispatch of copied functions -----------------------------------------------------------
//
// The native layouts run some stock functions as relocated copies (tools/gen_fncopy.py,
// gen_tuner8s.py, gen_publish*.py). The stock entry is overwritten by a jump to one of these stubs.
// The stub asks a C++ function which code to run (the copy for a native object, else a trampoline =
// the displaced entry bytes + a jump back into the stock function) and `ret`s into the answer with
// every register and the entry stack exactly as the game left them. Mechanism: reserve a stack slot,
// save all GPRs, call `target(kind, reg)`, store EAX over the slot (it becomes the return address),
// restore the GPRs, `ret`. Entry flags are dead and the x87 stack is empty at all these entries.
//
//   target  cdecl uintptr_t target(uint32_t kind, <reg value>)   (`kind` selects the function)
//   reg     the register (or stack operand) that identifies the object, read before anything moves

// target(kind, reg)
#define RS7_ENTRY_DISPATCH(ID, TARGET, KIND, REG)                                                                      \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm sub esp, 4 __asm pushad __asm push REG __asm push KIND __asm call TARGET __asm add esp,                  \
            8 __asm mov[esp + 32], eax __asm popad __asm ret                                                           \
    }
// target(kind, reg, [esp+4], [esp+8]): also forwards the first two stack arguments of the entry
// (they are read at [esp+44] before the first push, and again after it moved, so both are the
// entry's [esp+4] and [esp+8]; a REG operand on the stack is then at [esp+48]).
#define RS7_ENTRY_DISPATCH_ARGS(ID, TARGET, KIND, REG)                                                                 \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm sub esp,                                                                                                 \
            4 __asm pushad __asm push dword ptr[esp + 44] __asm push dword                                             \
                ptr[esp + 44] __asm push REG __asm push KIND __asm call TARGET __asm add esp,                          \
            16 __asm mov[esp + 32], eax __asm popad __asm ret                                                          \
    }
// target(kind, reg), and the entry EFLAGS survive: pushfd first, the saved flags dword stays on the
// stack below the return address slot until `popfd`; `ret 4` then drops it.
#define RS7_ENTRY_DISPATCH_KEEP_FLAGS(ID, TARGET, KIND, REG)                                                           \
    RS7_STUB(Rs7_Stub_##ID){                                                                                           \
        __asm pushfd __asm sub esp, 4 __asm pushad __asm push REG __asm push KIND __asm call TARGET __asm add esp,     \
        8 __asm mov[esp + 32], eax __asm popad __asm push dword ptr[esp + 4] __asm popfd __asm ret 4}

// ---- guarded replacements ------------------------------------------------------------------------

// `cmp <count reg>, [<info reg>+0Bh]; jae <stock reject target>` replaced by the same compare with
// the site's resume / target link variables (the publication ports keep the stock bounds check in
// place of a branch the patch had to move). Flags: the compare's, as stock.
#define RS7_BOUND_GUARD(ID, CMPREG, INFOREG)                                                                           \
    RS7_STUB(Rs7_Stub_##ID) {                                                                                          \
        __asm cmp CMPREG, byte ptr[INFOREG + 0Bh] __asm jae bad __asm jmp g_rs7_resume_##ID __asm bad                  \
            : __asm jmp g_rs7_target_##ID                                                                              \
    }

// ---- native GuitarStrings test --------------------------------------------------------------------

// A GuitarStrings object built by rs7string (core/gsnative.h) carries a 16-bit marker at +3Ah and
// the magic "RS7G" at +0ACh; a stock object or one built before the patch has neither. The values
// repeat gsnative.h's `kMarker` / `kMagic` / `kMarkerOff` / `kMagicOff` (static_assert in stubs_gs.cpp).
#define RS7_GS_MARKER_OFF 3Ah
#define RS7_GS_MARKER 3752h
#define RS7_GS_MAGIC_OFF 0ACh
#define RS7_GS_MAGIC 47375352h
// Falls through when [REG] is a native GuitarStrings, else jumps to NOT_NATIVE (CMP flags clobbered).
#define RS7_GS_IF_NATIVE(REG, NOT_NATIVE)                                                                              \
    __asm cmp word ptr[REG + RS7_GS_MARKER_OFF],                                                                       \
        RS7_GS_MARKER __asm jne NOT_NATIVE __asm cmp dword ptr[REG + RS7_GS_MAGIC_OFF],                                \
        RS7_GS_MAGIC __asm jne NOT_NATIVE
