#include "core/diagrams.h"
#include "core/fretgrid.h"
#include "core/nextgrid.h"
#include "core/pitch8.h"
#include "core/publish.h"
#include "core/publish_grid.h"
#include "generated/sites.generated.h"
#include "hooks/registry.h"
#include "test.h"
#include <cstring>

TEST(every_site_has_a_hook_with_the_variables_its_kind_needs) {
    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Site& s = kRs7Sites[i];
        const Rs7Hook* h = Rs7_FindHook(s.id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        if (s.kind == Rs7Kind::Ref || s.kind == Rs7Kind::CallRef) {
            CHECK(h->stub == nullptr);
            CHECK(h->target != nullptr);
            CHECK(h->ref);
            continue;
        }

        CHECK(!h->ref);

        if (s.kind == Rs7Kind::Bytes) {  // in-place byte patch: no stub, nothing to bind
            CHECK(h->stub == nullptr && h->resume == nullptr && h->target == nullptr);
            CHECK(s.write && std::strlen(s.write) == std::strlen(s.expect));
            continue;
        }

        if (h->callback) {
            CHECK(s.kind == Rs7Kind::Jmp);
            CHECK(h->stub == nullptr);  // resume / target stay bound for callbacks that set ctx.jump
            continue;
        }

        CHECK(h->stub != nullptr);
        if (h->publish && h->resume == nullptr) {
            continue;  // copied entries / fixed-ABI tail wrappers
        }

        // TA ends SelectString itself (`mov esp,ebp; pop ebp; ret`): no resume.
        // GS* entries of the copied functions (gsnative.h) jump to their copy or trampoline instead.
        const bool gsEntry = h->strs == kRs7StrNative && h->resume == nullptr;
        if (h->tuner == kRs7TunerNative) {
            continue;  // whole-function copy, no resume variable
        }

        if (s.kind == Rs7Kind::Jmp && std::strcmp(s.id, "TA") != 0 && std::strcmp(s.id, "PE") != 0 && !gsEntry) {
            CHECK(h->resume != nullptr);
        }
        if (s.kind == Rs7Kind::Rel32 || s.kind == Rs7Kind::Call || s.targetRelAt >= 0) {
            CHECK(h->target != nullptr);
        }
    }
}

TEST(ref_sites_are_never_written) {
    int refs = 0, callRefs = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Kind k = kRs7Sites[i].kind;
        if (k != Rs7Kind::Ref && k != Rs7Kind::CallRef) {
            continue;
        }

        (k == Rs7Kind::Ref ? refs : callRefs) += 1;
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);

        CHECK(h && !Rs7_ShouldPatch(*h, true) && !Rs7_ShouldPatch(*h, false));
    }

    CHECK_EQ(refs, 12);      // X_CM, X_TCS, X_THEAD, X_DT (7th string line)
                             // + X_VGSLOT, X_VGDI (virtual guitarist)
                             // + X_TMGR, X_FADEMGR (tuner; the B-phase refs went in 1.0)
                             // + X_DDSH (the 7-string model's textures)
                             // + X_MCMP, X_MTAB (the N-slot chord matcher) + X_PEGNAMES (N-slot PegSet)
    CHECK_EQ(callRefs, 37);  // X_SPAWN, X_ADV3 (neck targets, N9) + the string object calls the native strings use
                             // + X_WORLD, X_ROOT (entity root nodes, string7diag)
                             // + X_SETSCALE (F1/F2 fret bars), X_EULER (peg placement)
                             // + X_PEGSELECT, X_PEGPLACE, X_PEGLAYER (N-slot PegSet)
                             // + X_PULSE, X_LSPAWN, X_DESPAWN, X_BLACK, X_PEGRESET, X_MSG, X_FADEIN,
                             //   X_FADEOUT (tuner) + X_FIXSTR, X_STRPARAMS (edited headstock)
                             // + X_DETACH (the 7th string's ghost, issue #16)
                             // + X_NFCTOR, X_NFDTOR (the NiFile adapter for the model's textures)
                             // + X_SST (M4 chord frames, B2)
                             // + X_STRCON, X_STRASG, X_LOC, X_STRDTOR (tuning names, issue #15)
                             // + X_MSORT, X_MINS1, X_MINS2 (the N-slot chord matcher's sort)
                             // + X_EVREL (the tracker's eval release, RL1/RL2)
                             // + X_ATTACH (native GuitarStrings, issue #18 stage 4)
}

TEST(r7_r7b_n9_are_gone_the_native_grid_replaces_them) {
    CHECK(Rs7_FindHook("R7") == nullptr);
    CHECK(Rs7_FindHook("R7b") == nullptr && Rs7_FindHook("N9") == nullptr);  // 1.0
    CHECK(Rs7_FindHook("NG9") != nullptr);
}

TEST(unknown_id_has_no_hook) {
    CHECK(Rs7_FindHook("nope") == nullptr);
}

TEST(diagnostics_only_hooks_are_d2_d3_d5) {
    const uintptr_t savedDiagrams = g_rs7_diagrams;

    g_rs7_diagrams = 1;
    int diagOnly = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        if (kRs7Sites[i].kind == Rs7Kind::Ref || kRs7Sites[i].kind == Rs7Kind::CallRef) {
            continue;
        }

        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h) {
            continue;
        }
        if (h->publish) {
            continue;
        }
        if (h->vgOnly) {
            continue;  // vguitar_only_hooks_are_the_four_vg_sites
        }
        if (h->pegs) {
            continue;  // peg_hooks_follow_native_pegset
        }
        if (h->strs) {
            continue;  // string_hooks_follow_native_strings
        }
        if (h->fg) {
            continue;  // fretgrid_hooks_follow_native_fretgrid
        }
        if (!std::strncmp(h->id, "CM", 2)) {
            continue;  // colour_hooks_follow_native_colours (test_colournative.cpp)
        }
        if (h->pitch) {
            continue;  // pitch_hooks_follow_native_pitch
        }
        if (h->ng) {
            continue;  // nextgrid_hooks_follow_native_nextgrid
        }
        if (!std::strncmp(h->id, "CL", 2)) {
            continue;  // chordloops_default_on_and_requires_native_owner
        }
        if (h->tuner) {
            continue;  // tuner8_sites_are_on_by_default_and_require_native_strings
        }
        if (!std::strncmp(h->id, "LS", 2)) {
            continue;  // loadstep_probes_follow_publish_trace
        }
        if (!std::strncmp(h->id, "GK", 2)) {
            continue;  // clite_sites_follow_the_switch (test_clite.cpp)
        }

        const bool diag =
            std::strcmp(h->id, "D2") == 0 || std::strcmp(h->id, "D3") == 0 || std::strcmp(h->id, "D5") == 0;

        diagOnly += h->diagOnly ? 1 : 0;
        CHECK_EQ(h->diagOnly, diag);
        CHECK(Rs7_ShouldPatch(*h, true));             // diagnostics=1: every site is written
        CHECK_EQ(Rs7_ShouldPatch(*h, false), !diag);  // diagnostics=0: D2/D3/D5 resolved, not written
    }

    CHECK_EQ(diagOnly, 3);
    g_rs7_diagrams = savedDiagrams;
}

TEST(diagram_hooks_are_all_unpatched_by_default_and_enabled_together) {
    const uintptr_t saved = g_rs7_diagrams;

    for (const char* id : {"DGM", "DGS", "DGB", "TP13"}) {
        const auto* hook = Rs7_FindHook(id);

        CHECK(hook != nullptr);
        if (!hook) {
            continue;
        }

        g_rs7_diagrams = 0;
        CHECK(!Rs7_ShouldPatch(*hook, true));
        g_rs7_diagrams = 1;
        CHECK(Rs7_ShouldPatch(*hook, false));
    }

    g_rs7_diagrams = saved;
}

TEST(peg_hooks_follow_native_pegset) {
    int native = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isNative = !std::strncmp(h->id, "PS", 2);

        CHECK_EQ(h->pegs == kRs7PegNative, isNative);
        CHECK(h->pegs == 0 || h->pegs == kRs7PegNative);  // 1.0: the stage-2 legacy peg (PG*, PH*) is gone
        native += isNative;
        if (h->pegs) {
            CHECK_EQ(Rs7_ShouldPatch(*h, true, false, true), isNative);
        }
    }

    CHECK_EQ(native, 16);
}

TEST(string_hooks_follow_native_strings) {
    int native = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const char* id = h->id;
        const bool isNative = (!std::strncmp(id, "GS", 2) || !std::strncmp(id, "GI", 2) || !std::strncmp(id, "GRM", 3));

        CHECK_EQ(h->strs == kRs7StrNative, isNative);
        CHECK(h->strs == 0 || h->strs == kRs7StrNative);  // 1.0: the S7 object sites (N2-N4, N6, N7, N14-N17) are gone
        native += isNative;

        if (h->strs) {
            CHECK_EQ(Rs7_ShouldPatch(*h, true, false, true, true), isNative);
            CHECK(!Rs7_ShouldPatch(*h, true, false, true, false));
        }
    }

    CHECK_EQ(native, 26);
}

TEST(vguitar_only_hooks_are_the_four_vg_sites) {
    int vg = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool want = !std::strcmp(h->id, "TICK") || !std::strcmp(h->id, "JUD1") || !std::strcmp(h->id, "JUD2") ||
                          !std::strcmp(h->id, "AUD1");

        CHECK_EQ(h->vgOnly, want);
        if (!want) {
            continue;
        }

        ++vg;
        CHECK(!h->diagOnly);
        CHECK(!Rs7_ShouldPatch(*h, false) && !Rs7_ShouldPatch(*h, true));            // [test] vguitar=0
        CHECK(Rs7_ShouldPatch(*h, false, true) && Rs7_ShouldPatch(*h, true, true));  // vguitar=1
    }

    CHECK_EQ(vg, 4);
}

TEST(integrated_side_hooks_follow_both_string_modes) {
    const uintptr_t saved = g_rs7_diagrams;

    for (bool nativeStrings : {false, true}) {
        for (const char* id : {"W", "I1", "D2", "D3"}) {
            const auto* h = Rs7_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            CHECK(Rs7_ShouldPatch(*h, true, false, true, nativeStrings));
            if (h->diagOnly) {
                CHECK(!Rs7_ShouldPatch(*h, false, false, true, nativeStrings));
            }
        }

        for (const char* id : {"DGM", "DGS", "DGB", "TP13"}) {
            const auto* h = Rs7_FindHook(id);

            CHECK(h != nullptr);
            if (!h) {
                continue;
            }

            g_rs7_diagrams = 0;
            CHECK(!Rs7_ShouldPatch(*h, true, false, true, nativeStrings));
            g_rs7_diagrams = 1;
            CHECK(Rs7_ShouldPatch(*h, true, false, true, nativeStrings));
        }
    }

    g_rs7_diagrams = saved;
}

TEST(fretgrid_hooks_follow_native_fretgrid) {
    const uintptr_t saved = g_rs7_native_fretgrid;
    int n = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isFg = !std::strncmp(h->id, "FGR", 3) || !std::strcmp(h->id, "FGHK") ||
                          !std::strcmp(h->id, "FGCT");  // FGR<va> in-place patches, FGRB, the FGHK mid-hook

        CHECK_EQ(h->fg == kRs7FgNative, isFg);
        if (!isFg) {
            continue;
        }

        ++n;
        g_rs7_native_fretgrid = 0;
        CHECK(!Rs7_ShouldPatch(*h, true, true, true, true));  // default: not a single FretGrid site is written
        g_rs7_native_fretgrid = 1;
        CHECK(Rs7_ShouldPatch(*h, false, false, false, false));  // independent of the other switches
        CHECK(Rs7_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(n, 73);
    g_rs7_native_fretgrid = saved;
}

TEST(pitch_hooks_follow_native_pitch) {
    const uintptr_t saved = g_rs7_native_pitch;
    int n = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || !h->pitch) {
            continue;
        }

        ++n;
        CHECK_EQ(h->id[0], 'P');
        g_rs7_native_pitch = 0;
        CHECK(!Rs7_ShouldPatch(*h, true, true, true, true));  // default: never written
        g_rs7_native_pitch = 1;
        CHECK(Rs7_ShouldPatch(*h, true, false, false, false));
        CHECK(Rs7_ShouldPatch(*h, false, false, true, true));
    }

    CHECK_EQ(n, 11);  // PA1 PA2 PM0 PR0 PT1 PM1 PE0 PC1-PC4
    g_rs7_native_pitch = saved;
}

TEST(nextgrid_hooks_follow_native_nextgrid) {
    const uintptr_t saved = g_rs7_native_nextgrid;
    int n = 0;

    for (int i = 0; i < kRs7SiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7Sites[i].id);
        if (!h || h->ref) {
            continue;
        }

        const bool isNg = !std::strncmp(h->id, "NGB", 3) || !std::strcmp(h->id, "NG1") || !std::strcmp(h->id, "NGP") ||
                          !std::strcmp(h->id, "NG9");

        CHECK_EQ(h->ng == kRs7NgNative, isNg);
        if (!isNg) {
            continue;
        }

        ++n;

        for (bool strings : {false, true}) {
            g_rs7_native_nextgrid = 0;  // refused at startup: no NG site (and then nothing at all, 7-string unit)
            CHECK(!Rs7_ShouldPatch(*h, true, true, true, strings));
            g_rs7_native_nextgrid = 1;
            CHECK(Rs7_ShouldPatch(*h, false, false, false, strings));
        }
    }

    CHECK_EQ(n, 28);
    g_rs7_native_nextgrid = saved;
}

TEST(loadstep_probes_follow_publish_trace) {
    const uintptr_t saved = g_rs7_publish_trace;

    for (const char* id : {"LSA", "LSG", "LSC", "LSE"}) {
        const Rs7Hook* h = Rs7_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        CHECK(h->callback != nullptr);
        g_rs7_publish_trace = 0;
        CHECK(!Rs7_ShouldPatch(*h, true));
        g_rs7_publish_trace = 1;
        CHECK(Rs7_ShouldPatch(*h, false));
    }

    g_rs7_publish_trace = saved;
}
