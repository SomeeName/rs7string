#pragma once
#include "d3d/swap.h"
#include "d3d/texlist.h"
#include <windows.h>

// Runs forever on the startup worker thread: every 2 s until hooked, then every 10 s,
// walk the engine's texture list under the game's lock (X_TCS), add ramp candidates, retire stale ones
// (RsxrRamp_Reconcile), and >= 15 s after d3d9.dll appeared get the game's device from a listed texture
// (under the same lock) and hook its vtable. Once hooked, the slots are re-checked every second
// between the rounds (d3d9 rewrites its vtable copy at times, e.g. at a song load). Never throws,
// never leaves the game's lock held.
//
// opts.onDevice (the overlay, may be nullptr): called with the game's device, on this thread, right after the
// ramp hooks, and again on later rounds while RsxrPresent_Installed() is false (5 calls at most); opts.recheck
// every second after that.
struct RsxrWorkerOpts {
    void (*onDevice)(void* device) = nullptr;
    void (*recheck)() = nullptr;
};

void RsxrRamp_RunWorker(const RsxrSwapCfg& cfg, DWORD d3d9SeenTick, const RsxrWorkerOpts& opts = RsxrWorkerOpts{});

// After a clean walk (no read fault, no cycle/cap): candidates whose texture is no longer listed leave
// the table. Pending/Refused/Retry: retired here (a dead entry must not keep V6 off nor match a reused
// address). Active: RsxrCand::gone counts consecutive misses; at 2 the render thread is asked to release
// it (RsxrSwap_RequestRetire). Listed again: gone = 0. Logs one line per retired entry.
void RsxrRamp_Reconcile(const RsxrTexWalk& w);
// Tests: the same live-texture device validation used during a locked walk.
IDirect3DDevice9* RsxrRamp_DeviceOfForTests(uintptr_t tex, uintptr_t lo, uintptr_t hi);
