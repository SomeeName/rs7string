#pragma once
#include "platform/midhook.h"
#include <cstddef>
#include <cstdint>

// A patch site's hook record and the hook tables. The startup code looks a resolved site up by id
// (Rs7_FindHookIn) and writes it if the app layer's write conditions allow it (Rs7_ShouldPatch, src/hook_rules.h). The
// condition fields below are plain data: only those write conditions read them.

// The failure group a table's hooks belong to. Its values are the app's (model/groups.h converts); the platform
// only stores and compares them.
using Rs7Group = int;

struct Rs7Hook {
    const char* id;
    void* stub;         // nullptr for ref sites
    uintptr_t* resume;  // site + length, for stubs that jump back (nullptr if unused)
    uintptr_t* target;  // original branch/call target, or a ref site's dword (nullptr if unused)
    bool diagOnly;      // diagnostics only (D2/D3/D5): resolved always, written only with diagnostics=1
    bool ref;           // data reference (Rs7Kind::Ref): resolved always, never written
    bool vgOnly =
        false;     // virtual guitarist (TICK/JUD1/JUD2/AUD1): resolved always, written only with [test] vguitar=1
    int pegs = 0;  // written only with the native PegSet
    int strs = 0;  // written only with the native GuitarStrings
    Rs7Mid::Callback callback = nullptr;  // generic mid-hook (Jmp or Call sites); stub unused. resume / target are
                                          // still bound (site + length / the branch or call target) for callbacks that
                                          // assign ctx.jump
    int fg = 0;                           // written only with [neck] native_fretgrid=1
    int pitch = 0;                        // written only with [neck] native_pitch=1
    int ng = 0;                           // written only with the native next-note grid
    bool publish = false;                 // verified preparation ports, request + all five native prerequisites
    Rs7Mid::Mode midMode = Rs7Mid::Mode::Full;    // Light: no x87/SSE; Sse: SSE, no x87 (midhook.h, *_CALLBACKS)
    int tuner = 0;                                // written only with [neck] native_tuner=1
    Rs7Mid::Call midCall = Rs7Mid::Call::Before;  // Call sites: callback before or after the callee (midhook.h)
    Rs7Mid::Wrap midWrap{};                       // Call::Wrap: argument dwords, cleanup and scratch of the frame
    Rs7Mid::Callback midBefore = nullptr;         // Call::Wrap: the optional before callback (callback = after)

    bool IsMid() const { return callback || midBefore; }
};

// A Wrap row (ARCHITECTURE.md section 15 "Wrap placement"): `row` with the placement, its frame and callbacks (either
// may be nullptr, not both) in the given mode.
constexpr Rs7Hook Rs7_WrapRow(Rs7Hook row, Rs7Mid::Wrap wrap, Rs7Mid::Callback before, Rs7Mid::Callback after,
                              Rs7Mid::Mode mode = Rs7Mid::Mode::Full) {
    row.midCall = Rs7Mid::Call::Wrap;
    row.midWrap = wrap;
    row.midBefore = before;
    row.callback = after;
    row.midMode = mode;

    return row;
}

struct Rs7Resolved;
// Outside the freeze: the mid-hook entry of row `h` (IsMid) at its resolved Jmp, JmpAtomic8 (replays all of
// `expect`) or Call site, by its placement.
bool Rs7_PrepareMid(Rs7Mid::Hook& mid, const Rs7Resolved& r, const Rs7Hook& h);

// A feature's hook table (src/features/<name>/registration.cpp): all its hooks belong to `group`. The app
// layer lists every table once (kTables, src/registry.cpp).
struct Rs7HookTable {
    Rs7Group group;
    const Rs7Hook* hooks;
    size_t count;

    bool Has(const Rs7Hook& h) const {
        for (size_t i = 0; i < count; ++i) {
            if (&hooks[i] == &h) {
                return true;
            }
        }

        return false;
    }
};

// The hook with this id in the first of `n` tables that has one; nullptr if none.
const Rs7Hook* Rs7_FindHookIn(const Rs7HookTable* const* tables, size_t n, const char* id);
// The table of `n` that holds `h` (by address); nullptr if none.
const Rs7HookTable* Rs7_TableOf(const Rs7HookTable* const* tables, size_t n, const Rs7Hook& h);
