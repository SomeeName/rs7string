#include "d3d/worker.h"
#include "d3d/iids.h"
#include "d3d/sehread.h"
#include "features/colours/colours.h"
#include "model/groups.h"
#include "platform/log.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr DWORD kInstallDelayMs = 15000, kPollMs = 2000, kHookedPollMs = 10000, kRecheckMs = 1000;
constexpr unsigned kMaxRejectLines = 64;
const Rs7SehReader g_mem{};

const char* FamilyName(Rs7Family f) {
    return f == Rs7Family::Note ? "note" : "additive";
}

const char* StateName(Rs7CandState s) {
    switch (s) {
        case Rs7CandState::Pending:
            return "pending";
        case Rs7CandState::Active:
            return "active";
        case Rs7CandState::Refused:
            return "refused";
        case Rs7CandState::Retry:
            return "retry";
        case Rs7CandState::Busy:
            return "busy";
        case Rs7CandState::Retired:
            return "retired";
    }

    return "?";
}

// The game's device from a texture in the list. SEH-guarded; the caller holds the list lock, so the
// texture cannot be freed meanwhile. Returns the raw pointer (the game keeps the device alive) if its
// GetTexture/SetTexture slots point into d3d9.dll. (The vtable itself need not: d3d9.dll 10.0.26100
// gives every device its own vtable copy in private heap memory - observed by test_d3d_hook.cpp.)
IDirect3DDevice9* DeviceOf(uintptr_t tex, uintptr_t lo, uintptr_t hi) {
    __try {
        auto* base = reinterpret_cast<IDirect3DBaseTexture9*>(tex);
        IDirect3DTexture9* t = nullptr;
        if (FAILED(base->QueryInterface(kRs7IID_Texture9, reinterpret_cast<void**>(&t))) || !t) {
            return nullptr;
        }

        IDirect3DDevice9* dev = nullptr;
        const HRESULT hr = t->GetDevice(&dev);

        t->Release();
        if (FAILED(hr) || !dev) {
            return nullptr;
        }

        dev->Release();
        const uintptr_t* vt = *reinterpret_cast<uintptr_t* const*>(dev);
        const uintptr_t get = vt[64], set = vt[65];

        return get >= lo && get < hi && set >= lo && set < hi ? dev : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Holds the game's list lock for the walk (and the device acquisition) and leaves it on every path,
// a C++ exception or an SEH fault included (__finally). False: the lock stayed busy (20 x 5 ms).
bool LockedWalk(CRITICAL_SECTION* cs, uintptr_t head, uintptr_t lo, uintptr_t hi, Rs7TexWalk* w, bool wantDevice,
                IDirect3DDevice9** dev) {
    bool got = false;

    for (int i = 0; i < 20; ++i) {
        if (TryEnterCriticalSection(cs)) {
            got = true;
            break;
        }

        Sleep(5);
    }

    if (!got) {
        return false;
    }

    __try {
        Rs7Tex_WalkInto(g_mem, head, lo, hi, w);  // storage prepared outside the lock
        if (wantDevice && w->anyD3d) {
            *dev = DeviceOf(w->anyD3d, lo, hi);
        }
    } __finally {
        LeaveCriticalSection(cs);
    }

    return true;
}

struct Ctx {
    Rs7WorkerOpts opts;
    CRITICAL_SECTION* cs;
    uintptr_t head, lo, hi;
    DWORD d3d9Tick;
    bool hooked = false, busyLogged = false, fullLogged = false, noDeviceLogged = false;
    unsigned installFails = 0, rounds = 0;
    Rs7TexWalk walk;
    std::vector<uintptr_t> lastFound;
    std::vector<std::string> lastRejects, loggedRejects;
};

// Device finder only (overlay without the ramp copy): no candidates, no texture hooks.
void FindDeviceOnly(Ctx& x, bool wantDevice, IDirect3DDevice9* dev) {
    if (!wantDevice) {
        return;
    }

    if (!dev) {
        if (!x.noDeviceLogged) {
            Rs7Log("overlay: no device yet (no valid d3d texture in the list)");
        }
        x.noDeviceLogged = true;
        return;
    }

    x.hooked = true;
    if (x.opts.onDevice) {
        x.opts.onDevice(dev);
    }
}

// Each reject reason once, at most kMaxRejectLines of them.
void LogNewRejects(Ctx& x, const std::vector<std::string>& rejects) {
    for (const auto& r : rejects) {
        bool seen = false;

        for (const auto& l : x.loggedRejects) {
            seen = seen || l == r;
        }

        if (seen || x.loggedRejects.size() >= kMaxRejectLines) {
            continue;
        }

        x.loggedRejects.push_back(r);
        Rs7Log("ramp: reject %s", r.c_str());
    }
}

// Logs the walk on the first round and whenever its candidates or rejects changed.
void LogWalkIfChanged(Ctx& x) {
    const Rs7TexWalk& w = x.walk;
    std::vector<uintptr_t> found;

    found.reserve(w.found.size());

    for (const auto& f : w.found) {
        found.push_back(f.d3d);
    }

    if (x.rounds++ == 0 || found != x.lastFound || w.rejects != x.lastRejects) {
        unsigned note = 0, add = 0;

        for (const auto& f : w.found) {
            (f.family == Rs7Family::Note ? note : add)++;
        }

        Rs7Log(
            "ramp: texture list: %u nodes, %u source textures, %u name matches, %u candidates (note %u, additive %u)%s",
            w.nodes, w.sources, w.nameMatches, unsigned(w.found.size()), note, add,
            w.readFault    ? ", read fault"
            : w.cycleOrCap ? ", cycle or node cap"
                           : "");
        LogNewRejects(x, w.rejects);
        x.lastFound = found;
        x.lastRejects = w.rejects;
    }
}

// Adds the walk's new textures to the candidate table.
void AddCandidates(Ctx& x) {
    for (const auto& f : x.walk.found) {
        if (Rs7Cand_Find(reinterpret_cast<void*>(f.d3d))) {
            continue;
        }

        const int idx = Rs7Cand_Add(reinterpret_cast<void*>(f.d3d), f.family, f.path, f.obj);

        if (idx >= 0) {
            Rs7Log("ramp: candidate %s d3d=%08X obj=%08X fmt=%u '%s'", FamilyName(f.family), unsigned(f.d3d),
                   unsigned(f.obj), f.fmt, f.path);
        } else if (!x.fullLogged) {
            x.fullLogged = true;
            Rs7Log("ramp: candidate table full (%d): '%s' not swapped", kRs7MaxCands, f.path);
        }
    }
}

// Hooks the device's texture slots (and hands it to the overlay); retried next round on failure.
void InstallOnDevice(Ctx& x, IDirect3DDevice9* dev) {
    if (!dev) {
        if (!x.noDeviceLogged) {
            Rs7Log("ramp: no device yet (no valid d3d texture in the list)");
        }
        x.noDeviceLogged = true;
        return;
    }

    if (Rs7Swap_InstallOn(dev)) {
        x.hooked = true;
        Rs7Swap_UpdateV6("hooked");
        if (x.opts.onDevice) {
            x.opts.onDevice(dev);  // the overlay (Present/Reset)
        }
    } else if (++x.installFails <= 5) {
        Rs7Log("ramp: install failed (%u), retrying next round", x.installFails);
    }
}

void Round(Ctx& x) {
    if (!x.opts.ramp && x.hooked) {  // device finder only: nothing to walk for any more
        if (x.opts.recheck) {
            x.opts.recheck();
        }
        return;
    }

    const bool wantDevice = !x.hooked && GetTickCount() - x.d3d9Tick >= kInstallDelayMs;
    IDirect3DDevice9* dev = nullptr;

    if (!LockedWalk(x.cs, x.head, x.lo, x.hi, &x.walk, wantDevice, &dev)) {
        if (!x.busyLogged) {
            Rs7Log("ramp: texture list lock busy, skipping this round");
        }
        x.busyLogged = true;
        return;
    }

    if (!x.opts.ramp) {
        FindDeviceOnly(x, wantDevice, dev);
        return;
    }

    LogWalkIfChanged(x);
    AddCandidates(x);
    if (!x.walk.readFault && !x.walk.cycleOrCap) {
        Rs7Ramp_Reconcile(x.walk);
    }

    if (x.hooked) {
        Rs7Swap_Recheck();
        if (x.opts.recheck) {
            x.opts.recheck();
        }
    }

    if (wantDevice) {
        InstallOnDevice(x, dev);
    }
}

// Between rounds once hooked: only the slot checks (8-byte SEH reads). d3d9 rewrites its vtable copy
// at times (seen at the song load: every slot back to its entry point), and until the re-hook the ramp
// copy and the overlay are off, so check every second, not only every 10 s round.
void RecheckGuarded(Ctx* x) {
    __try {
        if (x->opts.ramp) {
            Rs7Swap_Recheck();
        }
        if (x->opts.recheck) {
            x->opts.recheck();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Rs7Ramp_CountFault();
    }
}

// No C++ objects here: an SEH fault or a C++ exception from a round is counted and the worker goes on
// (LockedWalk has already left the game's lock).
void RoundGuarded(Ctx* x) {
    __try {
        Round(*x);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Rs7Ramp_CountFault();
    }
}
}  // namespace

void Rs7Ramp_Reconcile(const Rs7TexWalk& w) {
    bool request = false, retired = false;

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        Rs7Cand* c = Rs7Cand_At(i);
        void* const d3d = c ? c->d3d : nullptr;
        if (!d3d) {
            continue;
        }

        bool listed = false;

        for (const auto& f : w.found) {
            listed = listed || (reinterpret_cast<void*>(f.d3d) == d3d && f.family == c->family);
        }

        if (listed) {
            InterlockedExchange(&c->gone, 0);
            continue;
        }

        const Rs7CandState s = c->state;

        if (s == Rs7CandState::Active) {
            if (InterlockedIncrement(&c->gone) >= 2) {
                request = true;
            }
        } else if (Rs7Cand_Retire(c, d3d, false)) {
            Rs7Log("ramp: candidate gone: %s d3d=%p '%s' (%s), slot freed", FamilyName(c->family), d3d, c->path,
                   StateName(s));
            retired = true;
        }
    }

    if (request) {
        Rs7Swap_RequestRetire();
    }
    if (retired) {
        Rs7Swap_UpdateV6("stale candidates retired");
    }
}

void Rs7Ramp_RunWorker(const Rs7SwapCfg& cfg, DWORD d3d9SeenTick, const Rs7WorkerOpts& opts) {
    try {
        Ctx x;

        x.opts = opts;
        x.cs = reinterpret_cast<CRITICAL_SECTION*>(g_rs7_texlist_cs);
        x.head = g_rs7_texlist_head;
        x.d3d9Tick = d3d9SeenTick;

        if (!x.cs || !x.head || !Rs7_ImageRange(GetModuleHandleW(L"d3d9.dll"), &x.lo, &x.hi)) {
            Rs7Log("ramp: worker not started (list lock %08X, head %08X, d3d9.dll %s): string 6 uses the preview row",
                   unsigned(g_rs7_texlist_cs), unsigned(g_rs7_texlist_head),
                   GetModuleHandleW(L"d3d9.dll") ? "unreadable" : "not loaded");
            return;
        }

        Rs7Swap_Configure(cfg);
        Rs7Swap_SetEngine(x.cs, x.lo, x.hi);
        Rs7Tex_Prepare(&x.walk);

        if (!opts.ramp) {
            Rs7Log("overlay: device finder started (no ramp copy): d3d9.dll %08X-%08X, list head %08X, hook after %u s",
                   unsigned(x.lo), unsigned(x.hi), unsigned(x.head), unsigned(kInstallDelayMs / 1000));
        } else {
            Rs7Log("ramp: worker started: d3d9.dll %08X-%08X, list head %08X, lock %08X, colours %06X/%06X, "
                   "allow_inexact=%d, hook after %u s, tid %lu",
                   unsigned(x.lo), unsigned(x.hi), unsigned(x.head), unsigned(g_rs7_texlist_cs), unsigned(cfg.rgb[0]),
                   unsigned(cfg.rgb[1]), int(cfg.allowInexact), unsigned(kInstallDelayMs / 1000), GetCurrentThreadId());
        }

        for (;;) {
            RoundGuarded(&x);

            if (!x.hooked) {
                Sleep(kPollMs);
                continue;
            }

            for (DWORD t = 0; t < kHookedPollMs; t += kRecheckMs) {
                Sleep(kRecheckMs);
                RecheckGuarded(&x);
            }
        }
    } catch (...) {
        // Never let an exception end the game's process from this thread.
    }
}
