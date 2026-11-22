#pragma once
#include "platform/midhook.h"
#include <cstddef>
#include <cstdint>

// A patch site's hook record and the hook tables. The startup code looks a resolved site up by id
// (Rsxr_FindHookIn) and writes it if the app layer's write conditions allow it (Rsxr_ShouldPatch, src/hook_rules.h).
// The condition fields below are plain data: only those write conditions read them.

// The failure group a table's hooks belong to. Its values are the app's (model/groups.h converts); the platform
// only stores and compares them.
using RsxrGroup = int;

struct RsxrHook {
    const char* id = nullptr;
    void* stub = nullptr;                  // nullptr for ref sites and mid-hooks
    uintptr_t* resume = nullptr;           // site + length, for stubs that jump back (nullptr if unused)
    uintptr_t* target = nullptr;           // original branch/call target, or a ref site's dword (nullptr if unused)
    RsxrMid::Callback callback = nullptr;  // generic mid-hook (Jmp or Call sites); stub unused. resume / target are
                                           // still bound (site + length / the branch or call target) for callbacks that
                                           // assign ctx.jump
    bool diagOnly = false;     // diagnostics only (D2/D3/D5): resolved always, written only with diagnostics=1
    bool ref = false;          // data reference (RsxrKind::Ref): resolved always, never written
    bool privateOnly = false;  // the private extras' sites (src/features/extras/extras.h): resolved always, written
                               // only when on
    uint32_t gates = 0;        // the app's write switches (model/gates.h); the platform only stores them
    RsxrMid::Mode midMode = RsxrMid::Mode::Full;    // Light: no x87/SSE; Sse: SSE, no x87 (midhook.h, *_CALLBACKS)
    RsxrMid::Call midCall = RsxrMid::Call::Before;  // Call sites: callback before or after the callee (midhook.h)
    RsxrMid::Wrap midWrap{};                        // Call::Wrap: argument dwords, cleanup and scratch of the frame
    RsxrMid::Callback midBefore = nullptr;          // Call::Wrap: the optional before callback (callback = after)

    bool IsMid() const { return callback || midBefore; }

    // Builders for the registration tables: Rsxr_Mid("ID", &cb).Light().Gate(kRsxrGatePitch).
    constexpr RsxrHook Gate(uint32_t bits) const {
        RsxrHook h = *this;

        h.gates |= bits;

        return h;
    }

    constexpr RsxrHook InMode(RsxrMid::Mode m) const {
        RsxrHook h = *this;

        h.midMode = m;

        return h;
    }

    constexpr RsxrHook Light() const { return InMode(RsxrMid::Mode::Light); }

    constexpr RsxrHook Sse() const { return InMode(RsxrMid::Mode::Sse); }

    constexpr RsxrHook After() const {
        RsxrHook h = *this;

        h.midCall = RsxrMid::Call::After;

        return h;
    }

    constexpr RsxrHook Diag() const {
        RsxrHook h = *this;

        h.diagOnly = true;

        return h;
    }

    constexpr RsxrHook Private() const {
        RsxrHook h = *this;

        h.privateOnly = true;

        return h;
    }
};

// A site that is resolved and bound but has no code of its own (a copy's anchor, a jump or call target).
constexpr RsxrHook Rsxr_Site(const char* id, uintptr_t* resume = nullptr, uintptr_t* target = nullptr) {
    RsxrHook h;

    h.id = id;
    h.resume = resume;
    h.target = target;

    return h;
}

// A generic mid-hook (midhook.h) at site `id`.
constexpr RsxrHook Rsxr_Mid(const char* id, RsxrMid::Callback cb, uintptr_t* resume = nullptr,
                            uintptr_t* target = nullptr) {
    RsxrHook h = Rsxr_Site(id, resume, target);

    h.callback = cb;

    return h;
}

// A naked stub (frozen; new hooks are mid-hooks).
constexpr RsxrHook Rsxr_Stub(const char* id, void* stub, uintptr_t* resume = nullptr, uintptr_t* target = nullptr) {
    RsxrHook h = Rsxr_Site(id, resume, target);

    h.stub = stub;

    return h;
}

// A data reference: `*target` receives the dword at the site; never written.
constexpr RsxrHook Rsxr_Ref(const char* id, uintptr_t* target) {
    RsxrHook h = Rsxr_Site(id, nullptr, target);

    h.ref = true;

    return h;
}

// A Wrap row (ARCHITECTURE.md section 14 "Wrap placement"): `row` with the placement, its frame and callbacks (either
// may be nullptr, not both) in the given mode.
constexpr RsxrHook Rsxr_WrapRow(RsxrHook row, RsxrMid::Wrap wrap, RsxrMid::Callback before, RsxrMid::Callback after,
                                RsxrMid::Mode mode = RsxrMid::Mode::Full) {
    row.midCall = RsxrMid::Call::Wrap;
    row.midWrap = wrap;
    row.midBefore = before;
    row.callback = after;
    row.midMode = mode;

    return row;
}

struct RsxrResolved;
// Outside the freeze: the mid-hook entry of row `h` (IsMid) at its resolved Jmp, JmpAtomic8 (replays all of
// `expect`) or Call site, by its placement.
bool Rsxr_PrepareMid(RsxrMid::Hook& mid, const RsxrResolved& r, const RsxrHook& h);

// A feature's hook table (src/features/<name>/registration.cpp): all its hooks belong to `group`. The app
// layer lists every table once (kTables, src/registry.cpp).
struct RsxrHookTable {
    RsxrGroup group;
    const RsxrHook* hooks;
    size_t count;

    bool Has(const RsxrHook& h) const {
        for (size_t i = 0; i < count; ++i) {
            if (&hooks[i] == &h) {
                return true;
            }
        }

        return false;
    }
};

// The hook with this id in the first of `n` tables that has one; nullptr if none.
const RsxrHook* Rsxr_FindHookIn(const RsxrHookTable* const* tables, size_t n, const char* id);
// The table of `n` that holds `h` (by address); nullptr if none.
const RsxrHookTable* Rsxr_TableOf(const RsxrHookTable* const* tables, size_t n, const RsxrHook& h);
