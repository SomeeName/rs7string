// The main batch of Rsxr_Startup (startup_sites.h): the site writers, the isolated patch rounds, the deferred FretGrid
// sites and the per-site log.
#include "startup_sites.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/extras/extras.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/strings/strings.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "platform/freeze.h"
#include "platform/hooktable.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/patch.h"
#include "platform/pattern.h"
#include "platform/resolve.h"
#include <windows.h>
#include <cstring>

namespace {

// Runs inside a thread freeze (platform/freeze.h): no allocation, no logging. A Bytes site's replacement is
// parsed beforehand (WriteBytesFor, outside the freeze).
RsxrWriteResult Apply(const RsxrResolved& r, const RsxrHook& h, uintptr_t midEntry, const std::vector<uint8_t>& bytes) {
    const uintptr_t stub = h.IsMid() ? midEntry : reinterpret_cast<uintptr_t>(h.stub);

    switch (r.site->kind) {
        case RsxrKind::Jmp:
            return Rsxr_WriteJmp(r.addr, static_cast<size_t>(r.site->length), stub);
        case RsxrKind::JmpAtomic8:
            return Rsxr_WriteJmpAtomic8(r.addr, stub);
        case RsxrKind::Rel32:
            return Rsxr_WriteRel32(r.addr, r.addr + 4, stub, true);
        case RsxrKind::Call:
            return Rsxr_WriteRel32(r.addr + 1, r.addr + 5, stub, false);
        case RsxrKind::Bytes:
            if (bytes.empty() || bytes.size() != r.original.size()) {
                return false;
            }

            return Rsxr_WriteBytes(r.addr, bytes.data(), bytes.size());
        case RsxrKind::Ref:
        case RsxrKind::CallRef:
            return true;  // never written (Rsxr_ShouldPatch excludes them anyway)
    }

    return false;
}

// Outside the freeze: a Bytes site's replacement bytes (empty: not a Bytes site or unparsable).
std::vector<uint8_t> WriteBytesFor(const RsxrResolved& r) {
    if (r.site->kind != RsxrKind::Bytes) {
        return {};
    }

    RsxrPattern w;
    if (!Rsxr_ParsePattern(r.site->write, w) || w.bytes.size() != r.original.size()) {
        return {};
    }

    return w.bytes;
}

// The bytes a site's apply / restore touch: the original bytes cover every kind's write.
RsxrRange RangeOf(const RsxrResolved& r) {
    return {r.addr, r.addr + r.original.size()};
}

// [neck] native_fretgrid=1: the FGR layout sites are written later, by the first call of the FGHK mid-hook (FGHK and
// FGCT, the mid-hooks, are written with the main batch) on the game's own thread (features/fretgrid/fretgrid.h,
// rsxrfg::OnGuitarUpdate); the resolved sites wait here.
struct DeferredSite {
    RsxrResolved r;
    const RsxrHook* h;
    std::vector<uint8_t> bytes;  // parsed at startup (Bytes sites)
};

std::vector<DeferredSite> g_deferredFg;
std::vector<RsxrRange> g_deferredRanges;  // built at startup with g_deferredFg

struct DeferredRound {
    size_t n;           // sites written before the failure (== size: all)
    size_t unrestored;  // rollbacks that failed the read-back
    size_t firstBad;    // index of the first of them
};

void WriteDeferred(void* p) {  // inside the freeze
    DeferredRound& d = *static_cast<DeferredRound*>(p);
    size_t n = 0;
    bool failedWritten = false, cleanupFailed = false;

    for (; n < g_deferredFg.size(); ++n) {
        const RsxrWriteResult result = Apply(g_deferredFg[n].r, *g_deferredFg[n].h, 0, g_deferredFg[n].bytes);

        if (!result) {
            failedWritten = result.written;
            cleanupFailed = !result.protectionRestored;
            break;
        }
    }

    d.n = n;
    if (n == g_deferredFg.size()) {
        return;
    }

    for (size_t j = 0; j < n + (failedWritten ? 1u : 0u); ++j) {
        const bool restored = Rsxr_RestoreBytes(g_deferredFg[j].r.addr, g_deferredFg[j].r.original.data(),
                                                g_deferredFg[j].r.original.size());

        if ((!restored || (j == n && cleanupFailed)) && d.unrestored++ == 0) {
            d.firstBad = j;
        }
    }

    if (cleanupFailed && !failedWritten && d.unrestored++ == 0) {
        d.firstBad = n;
    }
}

// The game (main) thread, first guitar update. The other threads (loaders, audio, our workers) are frozen
// while the FGR sites change.
rsxrfg::ApplyResult ApplyDeferredFg(bool lastAttempt) {
    DeferredRound d{0, 0, 0};
    RsxrFreezeStats st;
    const bool ran = Rsxr_RunFrozen(g_deferredRanges.data(), g_deferredRanges.size(), &WriteDeferred, &d, &st);
    char line[512];

    Rsxr_FreezeDescribe(st, line, sizeof line);
    RsxrLog("fretgrid: %s (%u deferred sites, game thread %lu)", line, unsigned(g_deferredFg.size()),
            GetCurrentThreadId());

    if (!ran) {
        if (!lastAttempt && !st.tooMany) {
            return rsxrfg::ApplyResult::Retry;  // nothing written; the game thread tries again later
        }

        Rsxr_DisableFeature(RsxrFeature::Fretgrid);
        return rsxrfg::ApplyResult::Failed;
    }

    if (d.n == g_deferredFg.size()) {
        return rsxrfg::ApplyResult::Applied;
    }

    if (d.unrestored) {  // never retried (FGHK writes once); the group stays off
        RsxrLog("fretgrid: UNRECOVERABLE %s (+%u more): restoring bytes or page protection failed after %s could not "
                "be written - feature fretgrid DISABLED",
                g_deferredFg[d.firstBad].r.site->id, unsigned(d.unrestored - 1), g_deferredFg[d.n].r.site->id);
    } else {
        RsxrLog("fretgrid: writing %s failed - the %u sites already written were restored",
                g_deferredFg[d.n].r.site->id, unsigned(d.n));
    }

    // Only the Fretgrid group fails (and what depends on its layout: native_publish, native_tuner).
    Rsxr_DisableFeature(RsxrFeature::Fretgrid);

    return rsxrfg::ApplyResult::Failed;
}

// Whether the site of `h` is written, given the features' switches now. A build without the private extras
// (src/features/extras/extras.h) has no privateOnly sites.
bool Writes(const RsxrHook& h) {
    return Rsxr_ShouldPatch(h, g_rsxr_diag != 0, RsxrPrivate_SitesOn(), g_rsxr_native_pegset != 0,
                            g_rsxr_native_strings != 0);
}

// [debug] skip_sites lists `id`.
bool Skipped(const std::string& skip, const char* id) {
    return skip.size() > 2 && skip.find("," + std::string(id) + ",") != std::string::npos;
}

// [neck] native_fretgrid: the FGR layout sites (not the FGHK / FGCT mid-hooks) are written later on the game
// thread (rsxrfg::OnGuitarUpdate); they are not part of the main batch.
bool DeferredFg(const RsxrResolved& r, const RsxrHook& h) {
    return (h.gates & kRsxrGateFretgrid) && std::strcmp(r.site->id, "FGHK") != 0 &&
           std::strcmp(r.site->id, "FGCT") != 0;
}
}  // namespace

// Prepares every enabled hook before any site is written. The mid-hook stubs live for the whole process: a rollback
// restores the sites but never frees code a thread could still be running. Startup runs once.
// A failing site disables only its own feature group (platform/isolate.h); the round then restarts with that group
// gone. Only a core failure aborts everything.
RsxrPatchResult RsxrStartup_WriteSites(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                                       const std::string& skip, std::vector<size_t>& written) {
    static RsxrMid::Hook midHooks[kRsxrSiteCount];
    RsxrPatchOps ops;
    std::vector<std::vector<uint8_t>> writeBytes(res.sites.size());
    uintptr_t colourCtor = 0, colourAlloc = 0;

    for (const RsxrResolved& r : res.sites) {
        if (!std::strcmp(r.site->id, "CM5184F6")) {
            colourCtor = r.addr;
        } else if (!std::strcmp(r.site->id, "CM518AC4")) {
            colourAlloc = r.addr;
        }
    }

    ops.count = res.sites.size();
    ops.group = [&](size_t i) {
        return Rsxr_Group(Rsxr_FeatureOf(*hooks[i]));
    };
    ops.name = [&](size_t i) {
        return res.sites[i].site->id;
    };
    ops.log = [](const std::string& m) {
        RsxrLog("%s", m.c_str());
    };
    ops.should = [&](size_t i) {
        if (!Writes(*hooks[i])) {
            return false;
        }

        if (Skipped(skip, res.sites[i].site->id)) {
            RsxrLog("site %s skipped ([debug] skip_sites)", res.sites[i].site->id);
            return false;
        }

        return !DeferredFg(res.sites[i], *hooks[i]);
    };
    ops.prepare = [&](size_t i) {
        if (res.sites[i].site->kind == RsxrKind::Bytes) {  // parsed here: apply runs inside the freeze
            if (writeBytes[i].empty()) {
                writeBytes[i] = WriteBytesFor(res.sites[i]);
            }
            if (writeBytes[i].empty()) {
                return false;
            }
        }

        if (!hooks[i]->IsMid() || midHooks[i].Entry()) {
            return true;
        }

        return Rsxr_PrepareMid(midHooks[i], res.sites[i], *hooks[i]);
    };
    ops.apply = [&](size_t i) {
        return Apply(res.sites[i], *hooks[i], midHooks[i].Entry(), writeBytes[i]);
    };
    ops.range = [&](size_t i) {
        return RangeOf(res.sites[i]);
    };
    ops.beforeWrite = [&] {
        return !g_rsxr_native_colours || RsxrColour_CanPatchFrozen(colourCtor, colourAlloc);
    };
    // Every round runs under a thread freeze; a restore is verified by a read-back.
    ops.restore = [&](size_t i) {
        const RsxrResolved& r = res.sites[i];

        return Rsxr_RestoreBytes(r.addr, r.original.data(), r.original.size());
    };

    const RsxrPatchResult result = Rsxr_PatchIsolated(kRsxrGroups, ops, written);

    if (result == RsxrPatchResult::PreconditionFailed) {
        g_rsxr_native_colours = 0;
        RsxrColour_LogPatchRefusal();
        RsxrLog("colour: stock ColourManager exists, is being built or could not be checked under the patch freeze "
                "- native_colours OFF; native 7-string unit has no fallback, main batch not written");
    }

    return result;
}

// The deferred FretGrid sites: the final round's feature state decides (a disabled Fretgrid group has none).
size_t RsxrStartup_DeferFretGrid(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                                 const std::string& skip) {
    g_deferredFg.clear();

    for (size_t i = 0; i < res.sites.size(); ++i) {
        if (DeferredFg(res.sites[i], *hooks[i]) && Writes(*hooks[i]) && !Skipped(skip, res.sites[i].site->id)) {
            g_deferredFg.push_back({res.sites[i], hooks[i], WriteBytesFor(res.sites[i])});
        }
    }

    g_deferredRanges.clear();

    for (const DeferredSite& d : g_deferredFg) {
        g_deferredRanges.push_back(RangeOf(d.r));
    }

    rsxrfg::SetDeferredApply(g_deferredFg.empty() ? nullptr : &ApplyDeferredFg);

    return g_deferredFg.size();
}

void RsxrStartup_LogSites(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks,
                          const std::string& skip) {
    for (size_t i = 0; i < res.sites.size(); ++i) {
        const RsxrResolved& r = res.sites[i];
        const RsxrHook& h = *hooks[i];

        if (h.ref) {
            RsxrLog("resolved %-7s at %08X -> %08X", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else if (Writes(h) && Skipped(skip, r.site->id)) {
            RsxrLog("resolved %-4s at %08X ([debug] skip_sites: not patched)", r.site->id, unsigned(r.addr));
        } else if (DeferredFg(r, h) && Writes(h)) {
            RsxrLog("deferred %-4s at %08X (written on the game thread by the first guitar update)", r.site->id,
                    unsigned(r.addr));
        } else if (Writes(h)) {
            RsxrLog("patched %-4s at %08X (original target %08X)", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else if (Rsxr_FeatureDisabled(Rsxr_FeatureOf(h))) {
            RsxrLog("resolved %-4s at %08X (group %s disabled: not patched)", r.site->id, unsigned(r.addr),
                    Rsxr_FeatureName(Rsxr_FeatureOf(h)));
        } else {
            RsxrLog("resolved %-4s at %08X (%s off: not patched)", r.site->id, unsigned(r.addr),
                    h.privateOnly                   ? "the private extras"
                    : !std::strncmp(h.id, "TV", 2)  ? "[test] force_*"
                    : (h.gates & kRsxrGatePegs)     ? "the native PegSet"
                    : (h.gates & kRsxrGateStrings)  ? "the native GuitarStrings"
                    : (h.gates & kRsxrGateFretgrid) ? "the native FretGrid"
                    : (h.gates & kRsxrGatePitch)    ? "the native pitch storage"
                    : (h.gates & kRsxrGateNextgrid) ? "the native next-note grid"
                                                    : "diagnostics");
        }
    }
}
