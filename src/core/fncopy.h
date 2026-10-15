#pragma once
#include <cstdint>

// Relocated copies of whole game functions (issue #18 stage 4, tools/gen_fncopy.py): the bytes of
// the unpacked exe with listed same-length instruction edits and calls into this DLL, rel32 branches
// out of the function relocated. Built once at startup from live memory, before any site is written.

struct Rs7FnEdit {
    uint16_t off;        // from the function start
    uint8_t len;         // bytes replaced
    const char* expect;  // original bytes ("8B 74 BB 04")
    const char* write;   // same-length replacement, "" for a call edit
    const void* sym;     // call edit: E8 rel32 to sym, NOP-filled to len (nullptr otherwise)
};

struct Rs7FnRel {
    uint16_t off;     // rel32 field in the function
    uint32_t target;  // its absolute target (outside the function)
};

struct Rs7FnSpec {
    const char* id;  // the entry site id (sites.json), e.g. "GSLD"
    uint32_t va;     // function start in the unpacked exe
    uint32_t len;
    uint32_t fnv;    // FNV-1a 32 of the original bytes
    uint32_t entry;  // bytes the entry jmp overwrites (whole instructions, >= 5, no branch)
    const Rs7FnEdit* edits;
    int nEdits;
    const Rs7FnRel* rels;
    int nRels;
};

uint32_t Rs7Fn_Fnv(const uint8_t* p, uint32_t n);
// Builds the copy of `spec` from the live function at `live` (must equal spec.va: the exe has a fixed
// image base) and a trampoline (the entry bytes + jmp live+entry). False (with a reason) when the live
// bytes differ from the generated ones or an edit's bytes do not match; nothing is allocated then.
bool Rs7Fn_Build(const Rs7FnSpec& spec, uintptr_t live, uintptr_t* copy, uintptr_t* tramp, char* why, int whyLen);
