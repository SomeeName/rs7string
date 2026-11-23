#include "features/colours/colours.h"
#include "features/diagrams/diagrams.h"
#include "features/fretgrid/fretgrid.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "features/tuner/tuner.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "platform/isolate.h"
#include "platform/resolve.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

namespace {
struct Fixture {
    // CMINIT / CMBUILD -> Colours, DGM -> Diagrams, R1 -> Core (real registry ids).
    const char* ids[4] = {"R2", "CMINIT", "CMBUILD", "DGM"};
    bool written[4] = {};
    bool failPrepare[4] = {}, failApply[4] = {};
    int restored = 0;
    std::vector<std::string> log;

    RsxrPatchOps Ops(const RsxrHook** hooks) {
        RsxrPatchOps o;

        o.count = 4;
        o.group = [=](size_t i) {
            return Rsxr_Group(Rsxr_FeatureOf(*hooks[i]));
        };
        o.name = [this](size_t i) {
            return ids[i];
        };
        o.should = [=](size_t i) {
            return Rsxr_ShouldPatch(*hooks[i], false, false, false, false);
        };
        o.prepare = [this](size_t i) {
            return !failPrepare[i];
        };
        o.apply = [this](size_t i) {
            if (failApply[i]) {
                return false;
            }

            written[i] = true;

            return true;
        };
        o.restore = [this](size_t i) {
            written[i] = false;
            ++restored;

            return true;
        };
        o.log = [this](const std::string& m) {
            log.push_back(m);
        };

        return o;
    }
};
}  // namespace

TEST(isolation_failing_colour_site_is_a_unit_failure_and_aborts) {
    // 1.0: native colours have no legacy fallback; a colour site failing leaves the game stock (nothing written).
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);
    rsxrtest::Restore restoreDiagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    g_rsxr_native_colours = 1;
    g_rsxr_diagrams = 1;
    Fixture f;
    const RsxrHook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rsxr_FindHook(f.ids[i]);
        CHECK(hooks[i] != nullptr);
        if (!hooks[i]) {
            return;
        }
    }

    CHECK(Rsxr_FeatureOf(*hooks[0]) == RsxrFeature::Core);
    CHECK(Rsxr_FeatureOf(*hooks[1]) == RsxrFeature::Colours);
    CHECK(Rsxr_FeatureOf(*hooks[3]) == RsxrFeature::Diagrams);
    CHECK(Rsxr_IsSevenUnit(RsxrFeature::Colours) && !Rsxr_IsSevenUnit(RsxrFeature::Diagrams));
    f.failPrepare[2] = true;  // CMBUILD cannot be relocated
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, f.Ops(hooks), written) == RsxrPatchResult::CoreFailed);
    CHECK(!f.written[0] && !f.written[1] && !f.written[2] && !f.written[3]);
    bool named = false;

    for (auto& l : f.log) {
        named |= l.find("CMBUILD") != std::string::npos && l.find("unit without fallback") != std::string::npos;
    }

    CHECK(named);
    Rsxr_ResetFeatureFailures();
}

TEST(isolation_failing_diagram_site_disables_only_diagrams) {
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);
    rsxrtest::Restore restoreDiagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    g_rsxr_native_colours = 1;
    g_rsxr_diagrams = 1;
    Fixture f;
    const RsxrHook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rsxr_FindHook(f.ids[i]);
    }

    f.failApply[3] = true;  // the diagram site's write fails after the others were written
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, f.Ops(hooks), written) == RsxrPatchResult::Ok);
    CHECK(f.restored >= 3);  // the round is undone, then rewritten without Diagrams
    CHECK(f.written[0] && f.written[1] && f.written[2] && !f.written[3]);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Diagrams) && g_rsxr_diagrams == 0);
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Colours) && g_rsxr_native_colours == 1);
    Rsxr_ResetFeatureFailures();
}

TEST(isolation_core_failure_still_aborts_and_restores) {
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);

    Rsxr_ResetFeatureFailures();
    g_rsxr_native_colours = 1;
    Fixture f;
    const RsxrHook* hooks[4];

    for (int i = 0; i < 4; ++i) {
        hooks[i] = Rsxr_FindHook(f.ids[i]);
    }

    f.failApply[3] = false;
    f.failApply[0] = false;
    f.failPrepare[0] = true;  // R1 is core
    std::vector<size_t> written;

    CHECK(Rsxr_PatchIsolated(kRsxrGroups, f.Ops(hooks), written) == RsxrPatchResult::CoreFailed);
    CHECK(!f.written[0] && !f.written[1] && !f.written[2] && !f.written[3]);
    Rsxr_ResetFeatureFailures();
}

TEST(isolation_resolve_failure_of_a_colour_site_aborts) {
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);
    rsxrtest::Restore restoreDiagrams(g_rsxr_diagrams);

    Rsxr_ResetFeatureFailures();
    g_rsxr_native_colours = 1;
    g_rsxr_diagrams = 1;
    const RsxrSite* cm = nullptr;
    const RsxrSite* core = nullptr;
    const RsxrSite* dg = nullptr;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        if (!std::strcmp(kRsxrSites[i].id, "CMUV")) {
            cm = &kRsxrSites[i];
        }
        if (!std::strcmp(kRsxrSites[i].id, "R2")) {
            core = &kRsxrSites[i];
        }
        if (!std::strcmp(kRsxrSites[i].id, "DGM")) {
            dg = &kRsxrSites[i];
        }
    }

    CHECK(cm && core && dg);
    if (!cm || !core || !dg) {
        return;
    }

    RsxrResolveResult res{false, {}, {"CMUV: 0 matches (need 1)"}, {RsxrResolved{core, 0x401000, 0, {}}}, {cm}};
    std::vector<std::string> log;

    CHECK(!Rsxr_IsolateResolveFailures(res, kRsxrGroups, [&](const std::string& m) { log.push_back(m); }));
    CHECK(!log.empty() && log[0].find("CMUV") != std::string::npos);
    Rsxr_ResetFeatureFailures();
    RsxrResolveResult other{false, {}, {"DGM: 0 matches (need 1)"}, {RsxrResolved{core, 0x401000, 0, {}}}, {dg}};

    CHECK(Rsxr_IsolateResolveFailures(other, kRsxrGroups, [](const std::string&) {}));
    CHECK(other.ok && other.sites.size() == 1);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Diagrams) && !Rsxr_FeatureDisabled(RsxrFeature::Colours));
    Rsxr_ResetFeatureFailures();
    RsxrResolveResult bad{false, {}, {"R1: 0 matches (need 1)"}, {}, {core}};

    CHECK(!Rsxr_IsolateResolveFailures(bad, kRsxrGroups, [](const std::string&) {}));
    Rsxr_ResetFeatureFailures();
}

TEST(publish_and_chordloops_sites_have_their_own_groups) {
    // A PUB* site is Publish, a CL* site is Chords, a pitch site is Pitch: no new site falls into Core,
    // so a failing site disables only its own group.
    size_t pub = 0, cl = 0;

    for (size_t i = 0; i < sizeof(kRsxrSites) / sizeof(kRsxrSites[0]); ++i) {
        const char* id = kRsxrSites[i].id;
        const bool isPub = !std::strncmp(id, "PUB", 3), isCl = !std::strncmp(id, "CL", 2);
        if (!isPub && !isCl) {
            continue;
        }

        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        if (isPub) {
            ++pub;
            CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Publish);
        } else {
            ++cl;
            CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Chords);
        }
    }

    CHECK_EQ(pub, 27u);  // + PUBFA, PUBBC
    CHECK_EQ(cl, 20u);   // + CLVE

    for (const char* id : {"PA1", "PA2", "PM0", "PR0", "PT1", "PM1", "PE0", "PC1", "PC2", "PC3", "PC4", "PBH"}) {
        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (h) {
            CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Pitch);
        }
    }
}

TEST(tuner8_sites_have_their_own_group) {
    // Every TU8* / TS8* site (session copies, mid-hooks) is Tuner; disabling it clears native_tuner only.
    size_t n = 0;

    for (size_t i = 0; i < sizeof(kRsxrSites) / sizeof(kRsxrSites[0]); ++i) {
        const char* id = kRsxrSites[i].id;
        if (std::strncmp(id, "TU8", 3) && std::strncmp(id, "TS8", 3)) {
            continue;
        }

        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);

        if (h) {
            ++n;
            CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Tuner);
        }
    }

    CHECK_EQ(n, 37u);
    Rsxr_ResetFeatureFailures();
    rsxrtest::Restore restoreTuner(g_rsxr_native_tuner);
    rsxrtest::Restore restoreColours(g_rsxr_native_colours);

    g_rsxr_native_tuner = 1;
    g_rsxr_native_colours = 1;
    CHECK(Rsxr_DisableFeature(RsxrFeature::Tuner));
    CHECK(g_rsxr_native_tuner == 0 && g_rsxr_native_colours == 1);
    Rsxr_ResetFeatureFailures();
    g_rsxr_native_tuner = 1;
    CHECK(Rsxr_DisableFeature(RsxrFeature::Colours));  // a prerequisite of the tuner copies goes: so does the tuner
    CHECK(g_rsxr_native_tuner == 0);
    Rsxr_ResetFeatureFailures();
}

TEST(fretgrid_failure_takes_native_publish_and_native_tuner_with_it) {
    // The deferred FretGrid layout (game thread) failing disables Fretgrid; publish and the tuner copies need it.
    Rsxr_ResetFeatureFailures();
    const uintptr_t sT = g_rsxr_native_tuner, sP = g_rsxr_publish_ports, sN = g_rsxr_native_publish,
                    sS = g_rsxr_native_strings, sF = g_rsxr_native_fretgrid, sC = g_rsxr_native_colours,
                    sI = g_rsxr_native_pitch, sG = g_rsxr_native_nextgrid;

    g_rsxr_native_tuner = g_rsxr_publish_ports = g_rsxr_native_publish = g_rsxr_native_strings =
        g_rsxr_native_fretgrid = g_rsxr_native_colours = g_rsxr_native_pitch = g_rsxr_native_nextgrid = 1;
    CHECK(Rsxr_DisableFeature(RsxrFeature::Fretgrid));
    CHECK(g_rsxr_native_fretgrid == 0 && g_rsxr_native_tuner == 0 && g_rsxr_publish_ports == 0 &&
          g_rsxr_native_publish == 0);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Publish) && !Rsxr_FeatureDisabled(RsxrFeature::Strings));
    Rsxr_ResetFeatureFailures();
    g_rsxr_native_tuner = sT;
    g_rsxr_publish_ports = sP;
    g_rsxr_native_publish = sN;
    g_rsxr_native_strings = sS;
    g_rsxr_native_fretgrid = sF;
    g_rsxr_native_colours = sC;
    g_rsxr_native_pitch = sI;
    g_rsxr_native_nextgrid = sG;
}
