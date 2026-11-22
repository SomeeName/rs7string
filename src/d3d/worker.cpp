#include "d3d/worker.h"
#include "d3d/hookutil.h"
#include "d3d/iids.h"
#include "d3d/present.h"
#include "d3d/sehread.h"
#include "d3d/swap_hook.h"
#include "features/colours/colours.h"
#include "model/groups.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <string>
#include <vector>

namespace {
constexpr DWORD kInstallDelayMs = 15000, kPollMs = 2000, kHookedPollMs = 10000, kRecheckMs = 1000;
constexpr unsigned kMaxRejectLines = 64, kMaxOverlayTries = 5;
constexpr int kLockTries = 20;
constexpr DWORD kLockWaitMs = 5;
const RsxrSehReader g_mem{};
using rsxrd3d::FamilyName;

const char* StateName(RsxrCandState s) {
    switch (s) {
        case RsxrCandState::Pending:
            return "pending";
        case RsxrCandState::Active:
            return "active";
        case RsxrCandState::Refused:
            return "refused";
        case RsxrCandState::Retry:
            return "retry";
        case RsxrCandState::Busy:
            return "busy";
        case RsxrCandState::Retired:
            return "retired";
    }

    return "?";
}

// The game's device from a texture in the list. SEH-guarded; the caller holds the list lock, so the
// texture cannot be freed meanwhile. Returns the raw pointer (the game keeps the device alive) if its
// GetTexture/SetTexture slots point into d3d9.dll or to our ramp thunks. (The vtable itself need not:
// d3d9.dll 10.0.26100 gives every device its own vtable copy in private heap memory - observed by test_d3d_hook.cpp.)
IDirect3DDevice9* DeviceOf(uintptr_t tex, uintptr_t lo, uintptr_t hi) {
    __try {
        auto* base = reinterpret_cast<IDirect3DBaseTexture9*>(tex);
        IDirect3DTexture9* t = nullptr;
        if (FAILED(base->QueryInterface(kRsxrIID_Texture9, reinterpret_cast<void**>(&t))) || !t) {
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
        const uintptr_t get = vt[rsxrswap::kSlotGet], set = vt[rsxrswap::kSlotSet];

        const bool getOk = (get >= lo && get < hi) || get == reinterpret_cast<uintptr_t>(&Rsxr_Thunk_GetTexture);
        const bool setOk = (set >= lo && set < hi) || set == reinterpret_cast<uintptr_t>(&Rsxr_Thunk_SetTexture);

        return getOk && setOk ? dev : nullptr;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// Holds the game's list lock for the walk (and the device acquisition) and leaves it on every path,
// a C++ exception or an SEH fault included (__finally). False: the lock stayed busy (kLockTries x kLockWaitMs).
bool LockedWalk(CRITICAL_SECTION* cs, uintptr_t head, uintptr_t lo, uintptr_t hi, RsxrTexWalk* w, bool wantDevice,
                IDirect3DDevice9** dev) {
    bool got = false;

    for (int i = 0; i < kLockTries; ++i) {
        if (TryEnterCriticalSection(cs)) {
            got = true;
            break;
        }

        Sleep(kLockWaitMs);
    }

    if (!got) {
        return false;
    }

    __try {
        RsxrTex_WalkInto(g_mem, head, lo, hi, w);  // storage prepared outside the lock
        if (wantDevice && w->anyD3d) {
            *dev = DeviceOf(w->anyD3d, lo, hi);
        }
    } __finally {
        LeaveCriticalSection(cs);
    }

    return true;
}

struct Ctx {
    RsxrWorkerOpts opts;
    CRITICAL_SECTION* cs;
    uintptr_t head, lo, hi;
    DWORD d3d9Tick;
    bool hooked = false, busyLogged = false, fullLogged = false, noDeviceLogged = false;
    unsigned installFails = 0, rounds = 0, overlayTries = 0;
    IDirect3DDevice9* dev = nullptr;  // the hooked device (the game keeps it alive)
    RsxrTexWalk walk;
    std::vector<uintptr_t> lastFound;
    std::vector<std::string> lastRejects, loggedRejects;
};

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
        RsxrLog("ramp: reject %s", r.c_str());
    }
}

// Logs the walk on the first round and whenever its candidates or rejects changed.
void LogWalkIfChanged(Ctx& x) {
    const RsxrTexWalk& w = x.walk;
    std::vector<uintptr_t> found;

    found.reserve(w.found.size());

    for (const auto& f : w.found) {
        found.push_back(f.d3d);
    }

    if (x.rounds++ == 0 || found != x.lastFound || w.rejects != x.lastRejects) {
        unsigned note = 0, add = 0, emb = 0;

        for (const auto& f : w.found) {
            (f.family == RsxrFamily::Note ? note : f.family == RsxrFamily::Additive ? add : emb)++;
        }

        RsxrLog("ramp: texture list: %u nodes, %u source textures, %u name matches, %u candidates (note %u, additive "
                "%u, embedded %u)%s",
                w.nodes, w.sources, w.nameMatches, unsigned(w.found.size()), note, add, emb,
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
        if (RsxrCand_Find(reinterpret_cast<void*>(f.d3d))) {
            continue;
        }

        const int idx = RsxrCand_Add(reinterpret_cast<void*>(f.d3d), f.family, f.path, f.obj);

        if (idx >= 0) {
            RsxrLog("ramp: candidate %s d3d=%08X obj=%08X fmt=%u '%s'", FamilyName(f.family), unsigned(f.d3d),
                    unsigned(f.obj), f.fmt, f.path);
        } else if (!x.fullLogged) {
            x.fullLogged = true;
            RsxrLog("ramp: candidate table full (%d): '%s' not swapped", kRsxrMaxCands, f.path);
        }
    }
}

// Hooks the device's texture slots (and hands it to the overlay); retried next round on failure.
void InstallOnDevice(Ctx& x, IDirect3DDevice9* dev) {
    if (!dev) {
        if (!x.noDeviceLogged) {
            RsxrLog("ramp: no device yet (no valid d3d texture in the list)");
        }
        x.noDeviceLogged = true;
        return;
    }

    if (RsxrSwap_InstallOn(dev)) {
        x.hooked = true;
        x.dev = dev;
        RsxrSwap_UpdateV6("hooked");

        if (x.opts.onDevice) {
            ++x.overlayTries;
            x.opts.onDevice(dev);  // the overlay (Present/Reset)
        }
    } else if (++x.installFails <= 5) {
        RsxrLog("ramp: install failed (%u), retrying next round", x.installFails);
    }
}

void Round(Ctx& x) {
    const bool wantDevice = !x.hooked && GetTickCount() - x.d3d9Tick >= kInstallDelayMs;
    const bool retryOverlay =
        x.hooked && x.opts.onDevice && !RsxrPresent_Installed() && x.overlayTries < kMaxOverlayTries;
    IDirect3DDevice9* dev = nullptr;

    // The overlay retry takes the device from this walk (a live texture's), not the pointer cached at the install.
    if (!LockedWalk(x.cs, x.head, x.lo, x.hi, &x.walk, wantDevice || retryOverlay, &dev)) {
        if (!x.busyLogged) {
            RsxrLog("ramp: texture list lock busy, skipping this round");
        }
        x.busyLogged = true;
        return;
    }

    LogWalkIfChanged(x);
    if (!x.walk.readFault && !x.walk.cycleOrCap) {
        RsxrRamp_Reconcile(x.walk);
    }
    AddCandidates(x);  // reconcile identity changes before deciding an address is already present

    if (wantDevice) {
        InstallOnDevice(x, dev);
    } else if (retryOverlay && dev && dev == x.dev) {
        ++x.overlayTries;
        x.opts.onDevice(dev);  // the ramp hook is in, the overlay's failed: retry a few rounds on the same device
    }
}

// Between rounds once hooked: only the slot checks (8-byte SEH reads). d3d9 rewrites its vtable copy
// at times (seen at the song load: every slot back to its entry point), and until the re-hook the ramp
// copy and the overlay are off, so check every second, not only every 10 s round.
void RecheckGuarded(Ctx* x) {
    __try {
        RsxrSwap_Recheck();
        if (x->opts.recheck) {
            x->opts.recheck();
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        RsxrRamp_CountFault();
    }
}

// An SEH fault or a C++ exception from a round is counted and the worker goes on (LockedWalk has already
// left the game's lock). Unwinding past __except runs no destructors: a fault inside Round leaks that
// round's temporaries (vectors, strings); Ctx stays valid and is reused.
void RoundGuarded(Ctx* x) {
    __try {
        Round(*x);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        RsxrRamp_CountFault();
    }
}
}  // namespace

void RsxrRamp_Reconcile(const RsxrTexWalk& w) {
    bool request = false, retired = false;

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        RsxrCand* c = RsxrCand_At(i);
        void* const d3d = c ? c->d3d : nullptr;
        if (!d3d) {
            continue;
        }

        bool listed = false;

        for (const auto& f : w.found) {
            listed = listed || (reinterpret_cast<void*>(f.d3d) == d3d && f.family == c->family && f.obj == c->obj);
        }

        if (listed) {
            InterlockedExchange(&c->gone, 0);
            continue;
        }

        const RsxrCandState s = c->state;

        if (s == RsxrCandState::Active) {
            if (InterlockedIncrement(&c->gone) >= 2) {
                request = true;
            }
        } else if (RsxrCand_Retire(c, d3d, false)) {
            RsxrLog("ramp: candidate gone: %s d3d=%p '%s' (%s), slot freed", FamilyName(c->family), d3d, c->path,
                    StateName(s));
            retired = true;
        }
    }

    if (request) {
        RsxrSwap_RequestRetire();
    }
    if (retired) {
        RsxrSwap_UpdateV6("stale candidates retired");
    }
}

IDirect3DDevice9* RsxrRamp_DeviceOfForTests(uintptr_t tex, uintptr_t lo, uintptr_t hi) {
    return DeviceOf(tex, lo, hi);
}

void RsxrRamp_RunWorker(const RsxrSwapCfg& cfg, DWORD d3d9SeenTick, const RsxrWorkerOpts& opts) {
    try {
        Ctx x;

        x.opts = opts;
        x.cs = reinterpret_cast<CRITICAL_SECTION*>(g_rsxr_texlist_cs);
        x.head = g_rsxr_texlist_head;
        x.d3d9Tick = d3d9SeenTick;

        if (!x.cs || !x.head || !Rsxr_ImageRange(GetModuleHandleW(L"d3d9.dll"), &x.lo, &x.hi)) {
            RsxrLog("ramp: worker not started (list lock %08X, head %08X, d3d9.dll %s): string 6 uses the preview row",
                    unsigned(g_rsxr_texlist_cs), unsigned(g_rsxr_texlist_head),
                    GetModuleHandleW(L"d3d9.dll") ? "unreadable" : "not loaded");
            return;
        }

        RsxrSwap_Configure(cfg);
        RsxrSwap_SetEngine(x.cs, x.lo, x.hi);
        RsxrTex_Prepare(&x.walk);

        RsxrLog("ramp: worker started: d3d9.dll %08X-%08X, list head %08X, lock %08X, colours %06X/%06X, "
                "hook after %u s, tid %lu",
                unsigned(x.lo), unsigned(x.hi), unsigned(x.head), unsigned(g_rsxr_texlist_cs), unsigned(cfg.rgb[0]),
                unsigned(cfg.rgb[1]), unsigned(kInstallDelayMs / 1000), GetCurrentThreadId());

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
        // The setup above (Ctx, RsxrTex_Prepare's reserve, logging) may throw std::bad_alloc; the rounds catch
        // their own (RoundGuarded, RecheckGuarded). Never let an exception end the game's process from this thread.
    }
}
