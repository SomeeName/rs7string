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
    const char* id = nullptr;
    void* stub = nullptr;                 // nullptr for ref sites and mid-hooks
    uintptr_t* resume = nullptr;          // site + length, for stubs that jump back (nullptr if unused)
    uintptr_t* target = nullptr;          // original branch/call target, or a ref site's dword (nullptr if unused)
    Rs7Mid::Callback callback = nullptr;  // generic mid-hook (Jmp or Call sites); stub unused. resume / target are
                                          // still bound (site + length / the branch or call target) for callbacks that
                                          // assign ctx.jump
    bool diagOnly = false;     // diagnostics only (D2/D3/D5): resolved always, written only with diagnostics=1
    bool ref = false;          // data reference (Rs7Kind::Ref): resolved always, never written
    bool privateOnly = false;  // the private extras' sites (src/features/extras/extras.h): resolved always, written
                               // only when on
    uint32_t gates = 0;        // the app's write switches (model/gates.h); the platform only stores them
    Rs7Mid::Mode midMode = Rs7Mid::Mode::Full;    // Light: no x87/SSE; Sse: SSE, no x87 (midhook.h, *_CALLBACKS)
    Rs7Mid::Call midCall = Rs7Mid::Call::Before;  // Call sites: callback before or after the callee (midhook.h)
    Rs7Mid::Wrap midWrap{};                       // Call::Wrap: argument dwords, cleanup and scratch of the frame
    Rs7Mid::Callback midBefore = nullptr;         // Call::Wrap: the optional before callback (callback = after)

    bool IsMid() const { return callback || midBefore; }

    // Builders for the registration tables: Rs7_Mid("ID", &cb).Light().Gate(kRs7GatePitch).
    constexpr Rs7Hook Gate(uint32_t bits) const {
        Rs7Hook h = *this;

        h.gates |= bits;

        return h;
    }

    constexpr Rs7Hook InMode(Rs7Mid::Mode m) const {
        Rs7Hook h = *this;

        h.midMode = m;

        return h;
    }

    constexpr Rs7Hook Light() const { return InMode(Rs7Mid::Mode::Light); }

    constexpr Rs7Hook Sse() const { return InMode(Rs7Mid::Mode::Sse); }

    constexpr Rs7Hook After() const {
        Rs7Hook h = *this;

        h.midCall = Rs7Mid::Call::After;

        return h;
    }

    constexpr Rs7Hook Diag() const {
        Rs7Hook h = *this;

        h.diagOnly = true;

        return h;
    }

    constexpr Rs7Hook Private() const {
        Rs7Hook h = *this;

        h.privateOnly = true;

        return h;
    }
};

// A site that is resolved and bound but has no code of its own (a copy's anchor, a jump or call target).
constexpr Rs7Hook Rs7_Site(const char* id, uintptr_t* resume = nullptr, uintptr_t* target = nullptr) {
    Rs7Hook h;

    h.id = id;
    h.resume = resume;
    h.target = target;

    return h;
}

// A generic mid-hook (midhook.h) at site `id`.
constexpr Rs7Hook Rs7_Mid(const char* id, Rs7Mid::Callback cb, uintptr_t* resume = nullptr,
                          uintptr_t* target = nullptr) {
    Rs7Hook h = Rs7_Site(id, resume, target);

    h.callback = cb;

    return h;
}

// A naked stub (frozen; new hooks are mid-hooks).
constexpr Rs7Hook Rs7_Stub(const char* id, void* stub, uintptr_t* resume = nullptr, uintptr_t* target = nullptr) {
    Rs7Hook h = Rs7_Site(id, resume, target);

    h.stub = stub;

    return h;
}

// A data reference: `*target` receives the dword at the site; never written.
constexpr Rs7Hook Rs7_Ref(const char* id, uintptr_t* target) {
    Rs7Hook h = Rs7_Site(id, nullptr, target);

    h.ref = true;

    return h;
}

// A Wrap row (ARCHITECTURE.md section 14 "Wrap placement"): `row` with the placement, its frame and callbacks (either
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
