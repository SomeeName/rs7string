// Publication of the colour palette into the game's ColourManager, and which thread may do it: CMINIT / CMBUILD
// (the manager's ctor end and build) record the manager's thread; every other caller only requests.
#include "features/colours/state.h"
#include "model/groups.h"
#include "platform/log.h"

// Publish() reads the stock colour pointers out of the manager. The game thread builds the manager (CMINIT,
// CMBUILD); publishing from any other thread can meet a half-built manager -> access violation on that thread
// (R2b, evidence/L1_7test.log "DIAG ... AV", the ramp thread died and the game hung; review #5).
// Rules (review #5, docs/ARCHITECTURE.md):
//  - only the thread that ran CMBUILD (State::cmTid, the game/render thread) ever writes into the manager;
//  - every other caller only sets State::dirty (RequestPublish). The ramp worker never calls PublishCurrent:
//    StoreV6 (rampstate.cpp) requests, and the game thread drains in CMBUILD, FGHK (0x592AC0, every guitar
//    update) and the SetTexture hook (render thread) via DrainPending;
//  - PublishCurrent (settings overlay, Present thread) still publishes directly when it runs on that thread,
//    and logs once + defers when it does not (the "assert").
// An SEH guard stays as the last net.

namespace {
using rs7colour::g_colour;

uint8_t* Manager() {  // *X_CM; the caller checked g_rs7_colormgr
    return *reinterpret_cast<uint8_t**>(g_rs7_colormgr);
}

bool PublishGuarded(uint8_t* mgr) {
    __try {
        rs7colour::Publish(mgr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!InterlockedExchange(&g_colour.loggedFault, 1)) {
            Rs7Log("colour: publish faulted on thread %lu (manager %p) - skipped", GetCurrentThreadId(),
                   static_cast<void*>(mgr));
        }
        return false;
    }
}
}  // namespace

void rs7colour::RequestPublish() {
    InterlockedExchange(&g_colour.dirty, 1);
}

bool rs7colour::PublishPending() {
    return g_colour.dirty != 0;
}

void rs7colour::DrainPending() {
    if (!g_colour.dirty || !g_rs7_native_colours || !g_rs7_colormgr) {
        return;
    }
    if (g_colour.cmTid == 0 || GetCurrentThreadId() != g_colour.cmTid) {
        return;  // not the manager's thread: leave it pending
    }

    uint8_t* mgr = Manager();
    if (!mgr || g_colour.built != mgr) {
        return;  // CMBUILD drains when it finishes this one
    }

    if (InterlockedExchange(&g_colour.dirty, 0)) {
        PublishGuarded(mgr);
    }
}

void rs7colour::PublishCurrent() {
    if (!g_rs7_native_colours || !g_rs7_colormgr) {
        return;
    }

    const DWORD tid = GetCurrentThreadId();

    if (g_colour.cmTid != 0 && tid != g_colour.cmTid) {  // not the game thread: never touch the manager here
        if (!InterlockedExchange(&g_colour.loggedForeign, 1)) {
            Rs7Log("colour: PublishCurrent on thread %lu, the manager belongs to thread %lu - deferred to it (logged "
                   "once)",
                   tid, static_cast<unsigned long>(g_colour.cmTid));
        }

        RequestPublish();
        return;
    }

    uint8_t* mgr = Manager();
    if (!mgr) {
        return;
    }

    if (g_colour.built != mgr) {  // not (yet) fully built by the game thread: CMBUILD will pick the new values up
        RequestPublish();
        return;
    }

    InterlockedExchange(&g_colour.dirty, 0);
    PublishGuarded(mgr);
}

void __cdecl Rs7Colour_CMINIT(Rs7Mid::Context& c) {
    g_colour.built = nullptr;
    void* const mgr = reinterpret_cast<void*>(c.esi);

    if (g_colour.loggedInit != mgr) {  // L6, once per manager
        g_colour.loggedInit = mgr;
        Rs7Log("colour: CMINIT %p tid %lu", mgr, GetCurrentThreadId());
    }

    rs7colour::Publish(reinterpret_cast<uint8_t*>(c.esi));
}

void __cdecl Rs7Colour_CMBUILD(Rs7Mid::Context& c) {
    auto* mgr = reinterpret_cast<uint8_t*>(c.esi);

    g_colour.cmTid = GetCurrentThreadId();

    if (g_colour.loggedBuild != mgr) {  // L6, once per manager
        g_colour.loggedBuild = mgr;
        Rs7Log("colour: CMBUILD %p tid %lu", static_cast<void*>(mgr), GetCurrentThreadId());
    }

    InterlockedExchange(&g_colour.dirty, 0);  // this publish carries every request made so far
    rs7colour::Publish(mgr);
    g_colour.built = mgr;
    if (InterlockedExchange(&g_colour.dirty, 0)) {
        rs7colour::Publish(mgr);  // a request arrived meanwhile
    }
}

void rs7colour::ResetThreadForTests() {
    g_colour.cmTid = 0;
    g_colour.dirty = 0;
    g_colour.built = nullptr;
    g_colour.loggedForeign = 0;
}
