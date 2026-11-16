#pragma once
#include "d3d/swap.h"
#include "d3d/texlist.h"
#include <windows.h>

// Runs forever on the startup worker thread: every 2 s until hooked, then every 10 s,
// walk the engine's texture list under the game's lock (X_TCS), add ramp candidates, retire stale ones
// (Rs7Ramp_Reconcile), and >= 15 s after d3d9.dll appeared get the game's device from a listed texture
// (under the same lock) and hook its vtable. Once hooked, the slots are re-checked every second
// between the rounds (d3d9 rewrites its vtable copy at times, e.g. at a song load). Never throws,
// never leaves the game's lock held.
//
// opts.onDevice (the overlay, may be nullptr): called with the game's device, on this thread, right after the
// ramp hooks, and again on later rounds while Rs7Present_Installed() is false (5 calls at most); opts.recheck
// every second after that.
struct Rs7WorkerOpts {
    void (*onDevice)(void* device) = nullptr;
    void (*recheck)() = nullptr;
};

void Rs7Ramp_RunWorker(const Rs7SwapCfg& cfg, DWORD d3d9SeenTick, const Rs7WorkerOpts& opts = Rs7WorkerOpts{});

// After a clean walk (no read fault, no cycle/cap): candidates whose texture is no longer listed leave
// the table. Pending/Refused/Retry: retired here (a dead entry must not keep V6 off nor match a reused
// address). Active: Rs7Cand::gone counts consecutive misses; at 2 the render thread is asked to release
// it (Rs7Swap_RequestRetire). Listed again: gone = 0. Logs one line per retired entry.
void Rs7Ramp_Reconcile(const Rs7TexWalk& w);
// Tests: the same live-texture device validation used during a locked walk.
IDirect3DDevice9* Rs7Ramp_DeviceOfForTests(uintptr_t tex, uintptr_t lo, uintptr_t hi);
