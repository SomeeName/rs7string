#pragma once

enum class Rs7Kind {
    Jmp,         // overwrite `length` bytes with E9 rel32 (+ NOPs); stub jumps back to site + length
    JmpAtomic8,  // 5-byte jmp inside one aligned qword, written with one locked exchange
    Rel32,       // the site is a rel32 operand (instruction ends at site + 4); retarget it
    Call,        // the site is E8 rel32; retarget the call
    Ref,         // data reference: never written; the resolved "target" is the dword at the site
    CallRef,     // code reference: never written; the site is E8 rel32, the "target" is its callee
    Bytes        // in-place byte patch: `write` replaces `expect` (same length; no stub)
};

struct Rs7Site {
    const char* id;
    const char* pattern;  // "8B 75 ?? 0C"
    int offset;           // site = pattern match + offset
    const char* expect;   // bytes at the site before patching, "83 FA 05 77 57"
    Rs7Kind kind;
    int length;              // bytes overwritten
    int targetRelAt;         // -1: no original branch target; else offset of its rel field from the site
    int targetRelSize;       // 1 or 4
    int targetInsnEnd;       // offset of the end of that branch instruction from the site
    const char* write = "";  // Bytes: the bytes written over `expect`, "" otherwise
};
