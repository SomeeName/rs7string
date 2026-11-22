// Publication of the colour palette into the game's ColourManager, and which thread may do it: CMINIT / CMBUILD
// (the manager's ctor end and build) record the manager's thread; every other caller only requests.
#include "features/colours/state.h"
#include "model/groups.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"

// Publish() reads the stock colour pointers out of the manager. The game thread builds the manager (CMINIT,
// CMBUILD); publishing from any other thread can meet a half-built manager -> access violation on that thread
// (seen in a 7-string test log: the ramp thread died and the game hung).
// Rules (docs/ARCHITECTURE.md):
//  - only the manager's thread (State::cmTid, recorded by CMINIT / CMBUILD) writes into the manager;
//  - every other caller only sets State::dirty (RequestPublish). The ramp worker never calls PublishCurrent:
//    StoreV6 (rampstate.cpp) requests, and the game thread drains in CMBUILD, FGHK (0x592AC0, every guitar
//    update) and the SetTexture hook (render thread) via DrainPending;
//  - PublishCurrent (settings overlay, Present thread) still publishes directly when it runs on that thread,
//    and logs once + defers when it does not (the "assert").
// An SEH guard stays as the last net.

namespace {
using rsxrcolour::g_colour;

void SetManagerThread() {
    AcquireSRWLockExclusive(&g_colour.paletteLock);
    g_colour.cmTid = GetCurrentThreadId();
    ReleaseSRWLockExclusive(&g_colour.paletteLock);
}

uint8_t* Manager() {  // *X_CM; the caller checked g_rsxr_colormgr
    uint8_t* mgr = nullptr;

    Rsxr_SafeCopyZero(&mgr, reinterpret_cast<const void*>(g_rsxr_colormgr), sizeof mgr);

    return mgr;
}

bool PublishGuarded(uint8_t* mgr) {
    __try {
        rsxrcolour::Publish(mgr);
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        if (!InterlockedExchange(&g_colour.loggedFault, 1)) {
            RsxrLog("colour: publish faulted on thread %lu (manager %p) - retry pending", GetCurrentThreadId(),
                    static_cast<void*>(mgr));
        }
        return false;
    }
}

void PublishRequested(uint8_t* mgr) {
    if (!InterlockedExchange(&g_colour.dirty, 0)) {
        return;
    }

    if (PublishGuarded(mgr)) {
        g_colour.publishFailures = 0;
        return;
    }

    rsxrcolour::RequestPublish();

    if (++g_colour.publishFailures >= 3) {
        g_colour.built = nullptr;
        RsxrLog("colour: manager %p failed publication three times - pending until the next build",
                static_cast<void*>(mgr));
    }
}
}  // namespace

void rsxrcolour::RequestPublish() {
    InterlockedExchange(&g_colour.dirty, 1);
}

bool rsxrcolour::PublishPending() {
    return g_colour.dirty != 0;
}

void rsxrcolour::DrainPending() {
    if (!g_colour.dirty || !g_rsxr_native_colours || !g_rsxr_colormgr) {
        return;
    }
    if (g_colour.cmTid == 0 || GetCurrentThreadId() != g_colour.cmTid) {
        return;  // not the manager's thread: leave it pending
    }

    uint8_t* mgr = Manager();
    if (!mgr || g_colour.built != mgr) {
        return;  // CMBUILD drains when it finishes this one
    }

    PublishRequested(mgr);
}

void rsxrcolour::PublishCurrent() {
    if (!g_rsxr_native_colours || !g_rsxr_colormgr) {
        return;
    }

    const DWORD tid = GetCurrentThreadId();

    if (g_colour.cmTid != 0 && tid != g_colour.cmTid) {  // not the game thread: never touch the manager here
        if (!InterlockedExchange(&g_colour.loggedForeign, 1)) {
            RsxrLog("colour: PublishCurrent on thread %lu, the manager belongs to thread %lu - deferred to it (logged "
                    "once)",
                    tid, static_cast<unsigned long>(g_colour.cmTid));
        }
    }

    RequestPublish();
    DrainPending();
}

void __cdecl RsxrColour_CMINIT(RsxrMid::Context& c) {
    SetManagerThread();
    g_colour.built = nullptr;
    g_colour.publishFailures = 0;
    g_colour.bassSwapped = false;  // a new manager (maybe at the old one's address): its slot 4 is stock
    void* const mgr = reinterpret_cast<void*>(c.esi);

    if (g_colour.loggedInit != mgr) {  // once per manager
        g_colour.loggedInit = mgr;
        RsxrLog("colour: CMINIT %p tid %lu", mgr, GetCurrentThreadId());
    }

    if (!PublishGuarded(reinterpret_cast<uint8_t*>(c.esi))) {
        rsxrcolour::RequestPublish();
    }
}

void __cdecl RsxrColour_CMBUILD(RsxrMid::Context& c) {
    auto* mgr = reinterpret_cast<uint8_t*>(c.esi);

    SetManagerThread();

    if (g_colour.loggedBuild != mgr) {  // once per manager
        g_colour.loggedBuild = mgr;
        RsxrLog("colour: CMBUILD %p tid %lu", static_cast<void*>(mgr), GetCurrentThreadId());
    }

    // With bassLowB on, CMINIT's Publish swapped slot 4. The build in between rewrote slot 4 (CONFIRMED, 0x518AF0
    // writes slots 0 to 5 of every state it fills), so this Publish keeps the build's entries and restores only the
    // ones that still hold the swap's values (native.cpp RestoreSlot4).
    g_colour.built = mgr;
    g_colour.publishFailures = 0;
    rsxrcolour::RequestPublish();
    PublishRequested(mgr);
}

void rsxrcolour::ResetThreadForTests() {
    g_colour.cmTid = 0;
    g_colour.dirty = 0;
    g_colour.built = nullptr;
    g_colour.loggedForeign = 0;
    g_colour.loggedFault = 0;
    g_colour.publishFailures = 0;
}
