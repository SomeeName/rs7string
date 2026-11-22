#pragma once

// The faults our own guarded code (a __try around a game read, a call into game or library code) turns into a failed
// call: bad memory, a bad instruction, an integer trap, and a C++ exception escaping game or library code. Anything
// else keeps searching: a breakpoint (a debugger), a guard page (the stack's or an allocator's), a stack overflow
// (swallowed, it would leave the thread without a guard page) and the game's own exception codes.
//   __try { ... } __except (Rsxr_FaultFilter(GetExceptionCode())) { ... }
inline int Rsxr_FaultFilter(unsigned long code) {
    switch (code) {
        case 0xC0000005u:  // EXCEPTION_ACCESS_VIOLATION
        case 0xC0000006u:  // EXCEPTION_IN_PAGE_ERROR
        case 0xC000001Du:  // EXCEPTION_ILLEGAL_INSTRUCTION
        case 0xC0000096u:  // EXCEPTION_PRIV_INSTRUCTION
        case 0xC0000094u:  // EXCEPTION_INT_DIVIDE_BY_ZERO
        case 0xC0000095u:  // EXCEPTION_INT_OVERFLOW
        case 0xC000008Cu:  // EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        case 0xE06D7363u:  // a C++ exception (e.g. bad_alloc)
            return 1;      // EXCEPTION_EXECUTE_HANDLER
        default:
            return 0;  // EXCEPTION_CONTINUE_SEARCH
    }
}
