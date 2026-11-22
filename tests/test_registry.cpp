#include "features/diagrams/diagrams.h"
#include "features/extras/extras.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "features/testdrive/testdrive.h"
#include "features/tuner/tuner.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "model/gates.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

TEST(every_site_has_a_hook_with_the_variables_its_kind_needs) {
    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrSite& s = kRsxrSites[i];
        const RsxrHook* h = Rsxr_FindHook(s.id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        if (s.kind == RsxrKind::Ref || s.kind == RsxrKind::CallRef) {
            CHECK(h->stub == nullptr);
            CHECK(h->target != nullptr);
            CHECK(h->ref);
            continue;
        }

        CHECK(!h->ref);

        if (s.kind == RsxrKind::Bytes) {  // in-place byte patch: no stub, nothing to bind
            CHECK(h->stub == nullptr && h->resume == nullptr && h->target == nullptr);
            CHECK(s.write && std::strlen(s.write) == std::strlen(s.expect));
            continue;
        }

        const bool wrap = h->midCall == RsxrMid::Call::Wrap;

        CHECK(s.kind == RsxrKind::Call || h->midCall == RsxrMid::Call::Before ||
              (wrap && s.kind == RsxrKind::Jmp));  // a Wrap on a jmp site wraps the function it enters
        CHECK(wrap || h->midBefore == nullptr);

        if (h->IsMid()) {
            CHECK(s.kind == RsxrKind::Jmp || s.kind == RsxrKind::JmpAtomic8 || s.kind == RsxrKind::Call);
            CHECK(h->stub == nullptr);  // resume / target stay bound for callbacks that set ctx.jump
            CHECK(s.kind != RsxrKind::Call || wrap || h->target != nullptr);  // the callee (a Wrap entry calls it)
            continue;
        }

        CHECK(h->stub != nullptr);
        if ((h->gates & kRsxrGatePublish) && h->resume == nullptr) {
            continue;  // copied entries / fixed-ABI tail wrappers
        }

        // TA ends SelectString itself (`mov esp,ebp; pop ebp; ret`): no resume.
        // GS* entries of the copied functions (features/strings/strings.h) jump to their copy or trampoline instead.
        const bool gsEntry = (h->gates & kRsxrGateStrings) != 0 && h->resume == nullptr;
        if (h->gates & kRsxrGateTuner) {
            continue;  // whole-function copy, no resume variable
        }

        if (s.kind == RsxrKind::Jmp && std::strcmp(s.id, "TA") != 0 && std::strcmp(s.id, "PE") != 0 && !gsEntry) {
            CHECK(h->resume != nullptr);
        }
        if (s.kind == RsxrKind::Rel32 || s.kind == RsxrKind::Call || s.targetRelAt >= 0) {
            CHECK(h->target != nullptr);
        }
    }
}

TEST(ref_sites_are_never_written) {
    int refs = 0, callRefs = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrKind k = kRsxrSites[i].kind;
        if (k != RsxrKind::Ref && k != RsxrKind::CallRef) {
            continue;
        }

        (k == RsxrKind::Ref ? refs : callRefs) += 1;
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);

        CHECK(h && !Rsxr_ShouldPatch(*h, true) && !Rsxr_ShouldPatch(*h, false));
    }

    CHECK_EQ(refs,
             14 + kRsxrPrivateRefSites);  // X_CM, X_TCS, X_THEAD, X_DT (7th string line), X_WTHR (W's threshold)
                                          // + the private extras' refs (src/features/extras/extras.h)
                                          // + X_TMGR, X_FADEMGR (tuner; the B-phase refs went in 1.0)
                                          // + X_DDSH (the 7-string model's textures)
                                          // + X_MCMP, X_MTAB (the N-slot chord matcher) + X_PEGNAMES (N-slot PegSet)
                                          // + ToneManager (extended tone apply)
                                          // + ToneContext (stock Tone Plan custom overrides)
                                          // + X_TBASS (the tuner's bass open MIDI table, extended bass)
    CHECK_EQ(callRefs, 41);  // MenuMovieRequest (menu loading retry) + X_SPAWN, X_ADV3 (neck targets, N9) + the string
                             // object calls the native strings use
                             // + X_WORLD, X_ROOT (entity root nodes, features/scene)
                             // + X_SETSCALE (F1/F2 fret bars), X_EULER (peg placement)
                             // + X_PEGSELECT, X_PEGPLACE, X_PEGLAYER (N-slot PegSet)
                             // + ToneRegisterCall, ToneResourceDestroy (extended tone registration)
                             // + X_PULSE, X_LSPAWN, X_DESPAWN, X_BLACK, X_PEGRESET, X_MSG, X_FADEIN,
                             //   X_FADEOUT (tuner) + X_FIXSTR, X_STRPARAMS (edited headstock)
                             // + X_DETACH (the 7th string's ghost, issue #16)
                             // + X_NFCTOR, X_NFDTOR (the NiFile adapter for the model's textures)
                             // + X_SST (M4 chord frames, B2)
                             // + X_STRCON, X_STRASG, X_LOC, X_STRDTOR (tuning names, issue #15)
                             // + X_MSORT, X_MINS1, X_MINS2 (the N-slot chord matcher's sort)
                             // + X_EVREL (the tracker's eval release, RL1/RL2)
                             // + X_ATTACH (native GuitarStrings, issue #18 stage 4), ToneApply (26 tone slots)
}

TEST(r7_r7b_n9_are_gone_the_native_grid_replaces_them) {
    CHECK(Rsxr_FindHook("R7") == nullptr);
    CHECK(Rsxr_FindHook("R7b") == nullptr && Rsxr_FindHook("N9") == nullptr);  // 1.0
    CHECK(Rsxr_FindHook("NG9") != nullptr);
}

TEST(unknown_id_has_no_hook) {
    CHECK(Rsxr_FindHook("nope") == nullptr);
}

TEST(diagnostics_only_hooks_are_d2_d3_d5) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    g_rsxr_diagrams = 1;
    int diagOnly = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        if (kRsxrSites[i].kind == RsxrKind::Ref || kRsxrSites[i].kind == RsxrKind::CallRef) {
            continue;
        }

        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h) {
            continue;
        }
        if (h->gates & kRsxrGatePublish) {
            continue;
        }
        if (h->privateOnly) {
            continue;  // the private extras test their own sites
        }
        if (h->gates & kRsxrGatePegs) {
            continue;  // peg_hooks_follow_native_pegset
        }
        if (h->gates & kRsxrGateStrings) {
            continue;  // string_hooks_follow_native_strings
        }
        if (h->gates & kRsxrGateFretgrid) {
            continue;  // fretgrid_hooks_follow_native_fretgrid
        }
        if (!std::strncmp(h->id, "CM", 2) || !std::strncmp(h->id, "CB", 2)) {
            continue;  // colour_hooks_are_the_cm_ports_and_the_cb_getters (tests/colours)
        }
        if (h->gates & kRsxrGatePitch) {
            continue;  // pitch_hooks_follow_native_pitch
        }
        if (h->gates & kRsxrGateNextgrid) {
            continue;  // nextgrid_hooks_follow_native_nextgrid
        }
        if (!std::strncmp(h->id, "CL", 2)) {
            continue;  // chordloops_require_native_strings
        }
        if (h->gates & kRsxrGateTuner) {
            continue;  // tuner8_sites_are_on_by_default_and_require_native_strings
        }
        if (!std::strncmp(h->id, "LS", 2)) {
            continue;  // loadstep_probes_follow_publish_trace
        }
        if (!std::strncmp(h->id, "GK", 2)) {
            continue;  // clite_sites_follow_the_switch (test_clite.cpp)
        }
        if (!std::strcmp(h->id, "GLW")) {
            continue;  // glow_hook_follows_the_rsmods_glow_switch (tests/colours)
        }
        if (!std::strncmp(h->id, "TV", 2)) {
            continue;  // test_variant_hooks_are_off_by_default
        }
        if (!std::strncmp(h->id, "E8", 2)) {
            continue;  // eight_sites_are_always_written
        }

        const bool diag =
            std::strcmp(h->id, "D2") == 0 || std::strcmp(h->id, "D3") == 0 || std::strcmp(h->id, "D5") == 0;

        diagOnly += h->diagOnly ? 1 : 0;
        CHECK_EQ(h->diagOnly, diag);
        CHECK(Rsxr_ShouldPatch(*h, true));             // diagnostics=1: every site is written
        CHECK_EQ(Rsxr_ShouldPatch(*h, false), !diag);  // diagnostics=0: D2/D3/D5 resolved, not written
    }

    CHECK_EQ(diagOnly, 3);
}

TEST(test_variant_hooks_are_off_by_default) {
    CHECK(!RsxrVariant_Get().Any());

    for (const char* id : {"TVCB", "TVGP"}) {
        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        CHECK(h && h->IsMid());
        CHECK(h && !Rsxr_ShouldPatch(*h, true));
    }
}

TEST(eight_sites_are_always_written) {
    int count = 0;

    for (const RsxrSite& site : kRsxrSites) {
        const RsxrHook* e8 = std::strncmp(site.id, "E8", 2) ? nullptr : Rsxr_FindHook(site.id);
        if (!e8) {
            continue;
        }

        ++count;
        CHECK(Rsxr_FindHook(site.id + 2) == nullptr);  // the seven-bound site it replaced is gone
        CHECK(Rsxr_ShouldPatch(*e8, true));
    }

    CHECK_EQ(count, 24);
}

TEST(diagram_hooks_are_all_unpatched_by_default_and_enabled_together) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    for (const char* id : {"DGM", "DGS", "DGB", "TP13"}) {
        const auto* hook = Rsxr_FindHook(id);

        CHECK(hook != nullptr);
        if (!hook) {
            continue;
        }

        g_rsxr_diagrams = 0;
        CHECK(!Rsxr_ShouldPatch(*hook, true));
        g_rsxr_diagrams = 1;
        CHECK(Rsxr_ShouldPatch(*hook, false));
    }
}

TEST(peg_hooks_follow_native_pegset) {
    int native = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isNative = !std::strncmp(h->id, "PS", 2);

        CHECK_EQ((h->gates & kRsxrGatePegs) != 0, isNative);
        native += isNative;
        if (h->gates & kRsxrGatePegs) {
            CHECK_EQ(Rsxr_ShouldPatch(*h, true, false, true), isNative);
        }
    }

    CHECK_EQ(native, 17);
}

TEST(string_hooks_follow_native_strings) {
    int native = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const char* id = h->id;
        const bool isNative = (!std::strncmp(id, "GS", 2) || !std::strncmp(id, "GI", 2) || !std::strncmp(id, "GRM", 3));

        CHECK_EQ((h->gates & kRsxrGateStrings) != 0, isNative);
        native += isNative;

        if (h->gates & kRsxrGateStrings) {
            CHECK_EQ(Rsxr_ShouldPatch(*h, true, false, true, true), isNative);
            CHECK(!Rsxr_ShouldPatch(*h, true, false, true, false));
        }
    }

    CHECK_EQ(native, 27);  // includes GIP1: profile assignment caps incomplete native allocations
}

TEST(integrated_side_hooks_follow_both_string_modes) {
    rsxrtest::Restore restore_g_rsxr_diagrams(g_rsxr_diagrams);

    for (bool nativeStrings : {false, true}) {
        for (const char* id : {"W", "I1", "D2", "D3"}) {
            const auto* h = Rsxr_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            CHECK(Rsxr_ShouldPatch(*h, true, false, true, nativeStrings));
            if (h->diagOnly) {
                CHECK(!Rsxr_ShouldPatch(*h, false, false, true, nativeStrings));
            }
        }

        for (const char* id : {"DGM", "DGS", "DGB", "TP13"}) {
            const auto* h = Rsxr_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            g_rsxr_diagrams = 0;
            CHECK(!Rsxr_ShouldPatch(*h, true, false, true, nativeStrings));
            g_rsxr_diagrams = 1;
            CHECK(Rsxr_ShouldPatch(*h, true, false, true, nativeStrings));
        }
    }
}

TEST(fretgrid_hooks_follow_native_fretgrid) {
    rsxrtest::Restore restore_g_rsxr_native_fretgrid(g_rsxr_native_fretgrid);
    int n = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isFg = !std::strncmp(h->id, "FGR", 3) || !std::strcmp(h->id, "FGHK") ||
                          !std::strcmp(h->id, "FGCT");  // FGR<va> in-place patches, FGRB, the FGHK mid-hook

        CHECK_EQ((h->gates & kRsxrGateFretgrid) != 0, isFg);
        if (!isFg) {
            continue;
        }

        ++n;
        g_rsxr_native_fretgrid = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true, true, true, true));  // default: not a single FretGrid site is written
        g_rsxr_native_fretgrid = 1;
        CHECK(Rsxr_ShouldPatch(*h, false, false, false, false));  // independent of the other switches
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(n, 73);
}

TEST(pitch_hooks_follow_native_pitch) {
    rsxrtest::Restore restore_g_rsxr_native_pitch(g_rsxr_native_pitch);
    int n = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || !(h->gates & kRsxrGatePitch)) {
            continue;
        }

        ++n;
        CHECK_EQ(h->id[0], 'P');
        g_rsxr_native_pitch = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true, true, true, true));  // default: never written
        g_rsxr_native_pitch = 1;
        CHECK(Rsxr_ShouldPatch(*h, true, false, false, false));
        CHECK(Rsxr_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(n, 12);  // PA1 PA2 PM0 PR0 PT1 PM1 PE0 PC1-PC4 PBH
}

TEST(nextgrid_hooks_follow_native_nextgrid) {
    rsxrtest::Restore restore_g_rsxr_native_nextgrid(g_rsxr_native_nextgrid);
    int n = 0;

    for (int i = 0; i < kRsxrSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrSites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isNg = !std::strncmp(h->id, "NGB", 3) || !std::strcmp(h->id, "NG1") || !std::strcmp(h->id, "NGP") ||
                          !std::strcmp(h->id, "NG9");

        CHECK_EQ((h->gates & kRsxrGateNextgrid) != 0, isNg);
        if (!isNg) {
            continue;
        }

        ++n;

        for (bool strings : {false, true}) {
            g_rsxr_native_nextgrid = 0;  // refused at startup: no NG site (and then nothing at all, 7-string unit)
            CHECK(!Rsxr_ShouldPatch(*h, true, true, true, strings));
            g_rsxr_native_nextgrid = 1;
            CHECK(Rsxr_ShouldPatch(*h, false, false, false, strings));
        }
    }

    CHECK_EQ(n, 28);
}

TEST(loadstep_probes_follow_publish_trace) {
    rsxrtest::Restore restore_g_rsxr_publish_trace(g_rsxr_publish_trace);

    for (const char* id : {"LSA", "LSG", "LSC", "LSE"}) {
        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        CHECK(h->callback != nullptr);
        g_rsxr_publish_trace = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true));
        g_rsxr_publish_trace = 1;
        CHECK(Rsxr_ShouldPatch(*h, false));
    }
}
