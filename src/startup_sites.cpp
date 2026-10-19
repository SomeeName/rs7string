// The main batch of Rs7_Startup (startup_sites.h): the site writers, the isolated patch rounds, the deferred FretGrid
// sites and the per-site log.
#include "startup_sites.h"
#include "features/diag/diag.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/strings/strings.h"
#include "features/vguit/vguit.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
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
bool Apply(const Rs7Resolved& r, const Rs7Hook& h, uintptr_t midEntry, const std::vector<uint8_t>& bytes) {
    const uintptr_t stub = h.IsMid() ? midEntry : reinterpret_cast<uintptr_t>(h.stub);

    switch (r.site->kind) {
        case Rs7Kind::Jmp:
            return Rs7_WriteJmp(r.addr, static_cast<size_t>(r.site->length), stub);
        case Rs7Kind::JmpAtomic8:
            return Rs7_WriteJmpAtomic8(r.addr, stub);
        case Rs7Kind::Rel32:
            return Rs7_WriteRel32(r.addr, r.addr + 4, stub, true);
        case Rs7Kind::Call:
            return Rs7_WriteRel32(r.addr + 1, r.addr + 5, stub, false);
        case Rs7Kind::Bytes:
            if (bytes.empty() || bytes.size() != r.original.size()) {
                return false;
            }

            return Rs7_WriteBytes(r.addr, bytes.data(), bytes.size());
        case Rs7Kind::Ref:
        case Rs7Kind::CallRef:
            return true;  // never written (Rs7_ShouldPatch excludes them anyway)
    }

    return false;
}

// Outside the freeze: a Bytes site's replacement bytes (empty: not a Bytes site or unparsable).
std::vector<uint8_t> WriteBytesFor(const Rs7Resolved& r) {
    if (r.site->kind != Rs7Kind::Bytes) {
        return {};
    }

    Rs7Pattern w;
    if (!Rs7_ParsePattern(r.site->write, w) || w.bytes.size() != r.original.size()) {
        return {};
    }

    return w.bytes;
}

// The bytes a site's apply / restore touch: the original bytes cover every kind's write.
Rs7Range RangeOf(const Rs7Resolved& r) {
    return {r.addr, r.addr + r.original.size()};
}

// [neck] native_fretgrid=1: the FGR layout sites are written later, by the first call of the FGHK mid-hook (FGHK and
// FGCT, the mid-hooks, are written with the main batch) on the game's own thread (features/fretgrid/fretgrid.h,
// rs7fg::OnGuitarUpdate); the resolved sites wait here.
struct DeferredSite {
    Rs7Resolved r;
    const Rs7Hook* h;
    std::vector<uint8_t> bytes;  // parsed at startup (Bytes sites)
};

std::vector<DeferredSite> g_deferredFg;
std::vector<Rs7Range> g_deferredRanges;  // built at startup with g_deferredFg

struct DeferredRound {
    size_t n;           // sites written before the failure (== size: all)
    size_t unrestored;  // review #8: rollbacks that failed the read-back
    size_t firstBad;    // index of the first of them
};

void WriteDeferred(void* p) {  // inside the freeze
    DeferredRound& d = *static_cast<DeferredRound*>(p);
    size_t n = 0;

    for (; n < g_deferredFg.size(); ++n) {
        if (!Apply(g_deferredFg[n].r, *g_deferredFg[n].h, 0, g_deferredFg[n].bytes)) {
            break;
        }
    }

    d.n = n;
    if (n == g_deferredFg.size()) {
        return;
    }

    for (size_t j = 0; j < n; ++j) {
        if (!Rs7_RestoreBytes(g_deferredFg[j].r.addr, g_deferredFg[j].r.original.data(),
                              g_deferredFg[j].r.original.size()) &&
            d.unrestored++ == 0) {
            d.firstBad = j;
        }
    }
}

// The game (main) thread, first guitar update. The other threads (loaders, audio, our workers) are frozen
// while the FGR sites change (review #9).
bool ApplyDeferredFg() {
    DeferredRound d{0, 0, 0};
    Rs7FreezeStats st;
    const bool ran = Rs7_RunFrozen(g_deferredRanges.data(), g_deferredRanges.size(), &WriteDeferred, &d, &st);
    char line[256];

    Rs7_FreezeDescribe(st, line, sizeof line);
    Rs7Log("fretgrid: %s (%u deferred sites, game thread %lu)", line, unsigned(g_deferredFg.size()),
           GetCurrentThreadId());

    if (!ran) {
        Rs7_DisableFeature(Rs7Feature::Fretgrid);
        return false;
    }

    if (d.n == g_deferredFg.size()) {
        return true;
    }

    if (d.unrestored) {  // review #8: never retried (FGHK writes once); the group stays off
        Rs7Log("fretgrid: UNRECOVERABLE %s (+%u more): restoring the original bytes failed after %s could not be "
               "written - "
               "sites stay written, feature fretgrid DISABLED",
               g_deferredFg[d.firstBad].r.site->id, unsigned(d.unrestored - 1), g_deferredFg[d.n].r.site->id);
    } else {
        Rs7Log("fretgrid: writing %s failed - the %u sites already written were restored", g_deferredFg[d.n].r.site->id,
               unsigned(d.n));
    }

    // Only the Fretgrid group fails (and what depends on its layout: native_publish, native_tuner).
    Rs7_DisableFeature(Rs7Feature::Fretgrid);

    return false;
}

// Whether the site of `h` is written, given the features' switches now.
bool Writes(const Rs7Hook& h) {
    return Rs7_ShouldPatch(h, g_rs7_diag != 0, Rs7Vg_Enabled(), g_rs7_native_pegset != 0, g_rs7_native_strings != 0);
}

// [debug] skip_sites lists `id`.
bool Skipped(const std::string& skip, const char* id) {
    return skip.size() > 2 && skip.find("," + std::string(id) + ",") != std::string::npos;
}

// [neck] native_fretgrid: the FGR layout sites (not the FGHK / FGCT mid-hooks) are written later on the game
// thread (rs7fg::OnGuitarUpdate); they are not part of the main batch.
bool DeferredFg(const Rs7Resolved& r, const Rs7Hook& h) {
    return h.fg == kRs7FgNative && std::strcmp(r.site->id, "FGHK") != 0 && std::strcmp(r.site->id, "FGCT") != 0;
}
}  // namespace

// Prepare every enabled generic hook before writing any site. The same Jmp sites.json entries retain sitecheck's
// whole-instruction/no-inbound checks. Process lifetime ownership: rollback restores sites, but never frees code
// that a thread could still be executing. Startup runs once.
// A failing site disables only its own feature group (platform/isolate.h); the round then restarts with that group
// gone. Only a core failure aborts everything.
Rs7PatchResult Rs7Startup_WriteSites(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks,
                                     const std::string& skip, std::vector<size_t>& todo) {
    static Rs7Mid::Hook midHooks[kRs7SiteCount];
    Rs7PatchOps ops;
    std::vector<std::vector<uint8_t>> writeBytes(res.sites.size());

    ops.count = res.sites.size();
    ops.group = [&](size_t i) {
        return Rs7_Group(Rs7_FeatureOf(*hooks[i]));
    };
    ops.name = [&](size_t i) {
        return res.sites[i].site->id;
    };
    ops.log = [](const std::string& m) {
        Rs7Log("%s", m.c_str());
    };
    ops.should = [&](size_t i) {
        if (!Writes(*hooks[i])) {
            return false;
        }

        if (Skipped(skip, res.sites[i].site->id)) {
            Rs7Log("site %s skipped ([debug] skip_sites)", res.sites[i].site->id);
            return false;
        }

        return !DeferredFg(res.sites[i], *hooks[i]);
    };
    ops.prepare = [&](size_t i) {
        if (res.sites[i].site->kind == Rs7Kind::Bytes) {  // parsed here: apply runs inside the freeze
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

        return Rs7_PrepareMid(midHooks[i], res.sites[i], *hooks[i]);
    };
    ops.apply = [&](size_t i) {
        return Apply(res.sites[i], *hooks[i], midHooks[i].Entry(), writeBytes[i]);
    };
    ops.range = [&](size_t i) {
        return RangeOf(res.sites[i]);
    };
    // review #9: every round under a thread freeze; review #8: read-back verified
    ops.restore = [&](size_t i) {
        const Rs7Resolved& r = res.sites[i];

        return Rs7_RestoreBytes(r.addr, r.original.data(), r.original.size());
    };

    return Rs7_PatchIsolated(kRs7Groups, ops, todo);
}

// The deferred FretGrid sites: the final round's feature state decides (a disabled Fretgrid group has none).
size_t Rs7Startup_DeferFretGrid(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks,
                                const std::string& skip) {
    g_deferredFg.clear();

    for (size_t i = 0; i < res.sites.size(); ++i) {
        if (DeferredFg(res.sites[i], *hooks[i]) && !hooks[i]->ref && Writes(*hooks[i]) &&
            !Skipped(skip, res.sites[i].site->id)) {
            g_deferredFg.push_back({res.sites[i], hooks[i], WriteBytesFor(res.sites[i])});
        }
    }

    g_deferredRanges.clear();

    for (const DeferredSite& d : g_deferredFg) {
        g_deferredRanges.push_back(RangeOf(d.r));
    }

    rs7fg::SetDeferredApply(g_deferredFg.empty() ? nullptr : &ApplyDeferredFg);

    return g_deferredFg.size();
}

void Rs7Startup_LogSites(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks) {
    for (size_t i = 0; i < res.sites.size(); ++i) {
        const Rs7Resolved& r = res.sites[i];
        const Rs7Hook& h = *hooks[i];

        if (h.ref) {
            Rs7Log("resolved %-7s at %08X -> %08X", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else if (DeferredFg(r, h) && Writes(h)) {
            Rs7Log("deferred %-4s at %08X (written on the game thread by the first guitar update)", r.site->id,
                   unsigned(r.addr));
        } else if (Writes(h)) {
            Rs7Log("patched %-4s at %08X (original target %08X)", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else {
            Rs7Log("resolved %-4s at %08X (%s off: not patched)", r.site->id, unsigned(r.addr),
                   h.vgOnly                       ? "[test] vguitar"
                   : !std::strncmp(h.id, "TV", 2) ? "[test] force_*"
                   : h.pegs == kRs7PegNative      ? "the native PegSet"
                   : h.strs == kRs7StrNative      ? "the native GuitarStrings"
                   : h.fg == kRs7FgNative         ? "the native FretGrid"
                   : h.pitch == kRs7PitchNative   ? "the native pitch storage"
                   : h.ng == kRs7NgNative         ? "the native next-note grid"
                                                  : "diagnostics");
        }
    }
}
