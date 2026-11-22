#include "features/clite/clite.h"
#include "features/headstock/headstock.h"
#include "features/publish/publish.h"
#include "features/strings/strings.h"
#include "features/tuner/tuner.h"
#include "hook_rules.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cstring>

extern "C" void __cdecl Rsxr_Mid_GKC(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_GKM(RsxrMid::Context& c);

TEST(clite_desired_count_follows_the_manifest_keys) {
    CHECK_EQ(rsxrclite::Desired(false, false), 6u);
    CHECK_EQ(rsxrclite::Desired(false, true), 6u);  // RsxrString7 alone is not an arrangement width
    CHECK_EQ(rsxrclite::Desired(true, false), 7u);
    CHECK_EQ(rsxrclite::Desired(true, true), 8u);
}

TEST(clite_peg_word_keeps_psq1_seven_with_native_pegset) {
    CHECK_EQ(rsxrclite::PegWord(6), 0x19180006u);                             // stock bytes C7 46 3A 06 00 18 19
    CHECK_EQ(rsxrclite::PegWord(rsxrclite::PegCount(6, true)), 0x19180007u);  // = PSQ1's write
    CHECK_EQ(rsxrclite::PegCount(6, false), 6u);
    CHECK_EQ(rsxrclite::PegCount(7, false), 7u);
    CHECK_EQ(rsxrclite::PegCount(8, true), 8u);
}

TEST(clite_headstock_loop_stays_in_the_six_slots) {
    // Rnd_Headstock_LoadStringMeshes 0x58E090: Headstock 0x38 bytes, loaded[6] at +18h, params[6] at +20h.
    CHECK_EQ(rsxrclite::HeadstockLoopCount(4), 4u);  // bass
    CHECK_EQ(rsxrclite::HeadstockLoopCount(6), 6u);
    CHECK_EQ(rsxrclite::HeadstockLoopCount(7), 6u);  // crash2.mdmp: slot 6 read params[6] at +38h
    CHECK_EQ(rsxrclite::HeadstockLoopCount(8), 6u);
    CHECK_EQ(rsxrclite::HeadstockLoopCount(0), 0u);
    CHECK_EQ(rsxrclite::HeadstockLoopCount(0x107), 6u);  // only the byte counts
    CHECK_EQ(rsxrclite::HeadstockLoopCount(5, 2), 4u);   // extended bass: the stock model's four segments
    CHECK_EQ(rsxrclite::HeadstockLoopCount(6, 2), 4u);
    CHECK_EQ(rsxrclite::BassDesired(false, false), 4u);
    CHECK_EQ(rsxrclite::BassDesired(true, false), 5u);
    CHECK_EQ(rsxrclite::BassDesired(true, true), 6u);
    CHECK_EQ(rsxrclite::BassDesired(false, true), 4u);  // high C alone is not a bass width
    CHECK_EQ(rsxrclite::BassCountFor(5, 0), 5u);
    CHECK_EQ(rsxrclite::BassCountFor(5, 1), 5u);  // co-op: player 2's bass is built for its own arrangement
    CHECK_EQ(rsxrclite::BassCountFor(6, 2), 4u);  // unknown player
    CHECK_EQ(rsxrclite::BassCountFor(7, 0), 4u);
    CHECK_EQ(rsxrclite::kGKH1Resume, 0x58E1BAu + 5u);
    CHECK_EQ(rsxrclite::kGKH2Resume, 0x58E36Du + 6u);
}

TEST(clite_rebuild_only_from_the_song_select_caller) {
    CHECK(rsxrclite::AllowedCaller(0x697A24));
    CHECK(!rsxrclite::AllowedCaller(0x74236F));
    CHECK(!rsxrclite::AllowedCaller(0));
}

TEST(clite_mode_entries_rebuild_a_seven_built_guitar_back_to_six) {
    using rsxrclite::RebuildTarget;

    // Song select: either way, either player, only on a change.
    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6), 7u);
    CHECK_EQ(RebuildTarget(0x697A24, 0, 6, 7), 6u);
    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 7), 0u);
    CHECK_EQ(RebuildTarget(0x697A24, 1, 7, 6), 7u);
    CHECK_EQ(RebuildTarget(0x697A24, 2, 7, 6), 0u);

    // Mode entries: down to 6 only.
    for (const auto& entry : rsxrclite::kRetModeEntries) {
        const uint32_t r = entry.ret;

        CHECK(rsxrclite::ModeEntry(r) != nullptr);
        CHECK_EQ(RebuildTarget(r, 0, 6, 7), 6u);
        CHECK_EQ(RebuildTarget(r, 0, 6, 8), 6u);
        CHECK_EQ(RebuildTarget(r, 0, 6, 6), 0u);  // already six: nothing
        CHECK_EQ(RebuildTarget(r, 0, 7, 6), 0u);  // never up from a mode entry
        CHECK_EQ(RebuildTarget(r, 0, 7, 7), 0u);  // a tuning with Rsxr keys keeps the build
        CHECK_EQ(RebuildTarget(r, 1, 6, 7), 6u);  // player 2's co-op build goes back to six as well
        CHECK_EQ(RebuildTarget(r, 2, 6, 7), 0u);
    }

    CHECK(std::strcmp(rsxrclite::ModeEntry(0x6F44BE), "Session Mode") == 0);
    CHECK(std::strcmp(rsxrclite::ModeEntry(0x6DBD09), "Session Mode") == 0);
    // Left out on purpose (they can run around a 7-string song): tuner re-stores, song options.
    const uint32_t out[] = {0x74236F, 0x7400B3, 0x742B24, 0x742C09, 0x69A7AF, 0};

    for (uint32_t r : out) {
        CHECK(rsxrclite::ModeEntry(r) == nullptr);
        CHECK(!rsxrclite::TunerMenu(r));
        CHECK_EQ(RebuildTarget(r, 0, 6, 7), 0u);
    }
}

TEST(clite_tuner_menu_rebuilds_to_the_picked_tuning_before_the_tuner) {
    // fix/menutuner (b): TuningOptionsMenu 0x745D90 stores (0x745E46 player 0, 0x745EC3 MP) then opens the tuner.
    using rsxrclite::RebuildTarget;

    for (uint32_t r : rsxrclite::kRetTunerMenu) {
        CHECK(rsxrclite::TunerMenu(r));
        CHECK(rsxrclite::ModeEntry(r) == nullptr);
        CHECK(!rsxrclite::AllowedCaller(r));
        CHECK_EQ(RebuildTarget(r, 0, 6, 7), 6u);  // the owner's crash: 6-string tuning, 7-built guitar
        CHECK_EQ(RebuildTarget(r, 0, 6, 8), 6u);
        CHECK_EQ(RebuildTarget(r, 0, 7, 6), 7u);  // a tuning with RsxrString6: up to 7
        CHECK_EQ(RebuildTarget(r, 0, 6, 6), 0u);  // unchanged: nothing
        CHECK_EQ(RebuildTarget(r, 0, 7, 7), 0u);
        CHECK_EQ(RebuildTarget(r, 1, 6, 7), 6u);               // player 2's guitar as well (co-op)
        CHECK_EQ(RebuildTarget(r, 2, 6, 7), 0u);               // unknown player: never
        CHECK_EQ(RebuildTarget(r, 0, 6, 7, true), 0u);         // a live tuner holds the guitar: never
        CHECK_EQ(RebuildTarget(r, 0, 6, 7, false, true), 0u);  // a song is loaded: never under it
    }

    // The song guard is the tuner menu's only: song select and mode entries ignore it.
    CHECK_EQ(RebuildTarget(0x697A24, 0, 7, 6, false, true), 7u);
    CHECK_EQ(RebuildTarget(0x6F44BE, 0, 6, 7, false, true), 6u);
}

TEST(clite_needs_the_native_tuner) {
    // The stock tuner session has six slots per string array; a 7-built guitar must not meet it.
    const uintptr_t sC = g_rsxr_clite, sT = g_rsxr_native_tuner, sP = g_rsxr_publish_ports, sN = g_rsxr_native_publish;

    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = 1;
    g_rsxr_native_tuner = 1;
    g_rsxr_publish_ports = 1;
    g_rsxr_native_publish = 1;
    CHECK(Rsxr_DisableFeature(RsxrFeature::Tuner));
    CHECK_EQ(g_rsxr_native_tuner, 0u);
    CHECK_EQ(g_rsxr_clite, 0u);
    CHECK_EQ(g_rsxr_native_publish, 0u);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Clite));
    CHECK(!Rsxr_ShouldPatch(*Rsxr_FindHook("GKT"), true, true, true, true));
    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = sC;
    g_rsxr_native_tuner = sT;
    g_rsxr_publish_ports = sP;
    g_rsxr_native_publish = sN;
}

TEST(clite_and_publish_go_when_native_pegs_are_lost_at_runtime) {
    // Review F1: a Pegs group failure after the initial configuration (a native PS* hook that fails to prepare/write)
    // must take clite and the ports with it: GKC would write peg request 7/8 while the next round omits the native
    // PegSet.
    const uintptr_t sC = g_rsxr_clite, sP = g_rsxr_publish_ports, sN = g_rsxr_native_publish, sG = g_rsxr_native_pegset,
                    sT = g_rsxr_native_tuner;

    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = 1;
    g_rsxr_publish_ports = 1;
    g_rsxr_native_publish = 1;
    g_rsxr_native_pegset = 1;
    g_rsxr_native_tuner = 1;
    const RsxrHook* gkc = Rsxr_FindHook("GKC");
    const RsxrHook* gke = Rsxr_FindHook("GKE");
    const RsxrHook* gkt = Rsxr_FindHook("GKT");

    CHECK(gkc && gke && gkt);

    if (gkc && gke && gkt) {
        CHECK(Rsxr_ShouldPatch(*gkc, true, true, true, true));  // the first round: native pegs, clite on
        CHECK(Rsxr_DisableFeature(RsxrFeature::Pegs));          // isolation after PSL1 failed to write
        CHECK_EQ(g_rsxr_native_pegset, 0u);
        CHECK_EQ(g_rsxr_clite, 0u);
        CHECK_EQ(g_rsxr_publish_ports, 0u);
        CHECK_EQ(g_rsxr_native_publish, 0u);
        CHECK(Rsxr_FeatureDisabled(RsxrFeature::Clite));
        CHECK(Rsxr_FeatureDisabled(RsxrFeature::Publish));
        // The next round (nativePegs = g_rsxr_native_pegset = 0): no GKC / GKE / GKT.
        CHECK(!Rsxr_ShouldPatch(*gkc, true, true, false, true));
        CHECK(!Rsxr_ShouldPatch(*gke, true, true, false, true));
        CHECK(!Rsxr_ShouldPatch(*gkt, true, true, false, true));
    }

    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = sC;
    g_rsxr_publish_ports = sP;
    g_rsxr_native_publish = sN;
    g_rsxr_native_pegset = sG;
    g_rsxr_native_tuner = sT;
}

TEST(clite_sites_follow_the_switch_and_replace_psq1) {
    rsxrtest::Restore restore_g_rsxr_clite(g_rsxr_clite);

    Rsxr_ResetFeatureFailures();
    const char* ids[] = {"GKC", "GKE", "GKM", "GKL", "GKT", "GKH1", "GKH2"};

    for (const char* id : ids) {
        const RsxrHook* h = Rsxr_FindHook(id);

        CHECK(h != nullptr);
        if (!h) {
            continue;
        }

        CHECK(h->callback != nullptr);
        CHECK(Rsxr_FeatureOf(*h) == RsxrFeature::Clite);
        g_rsxr_clite = 0;
        CHECK(!Rsxr_ShouldPatch(*h, true, true, true, true));
        g_rsxr_clite = 1;
        CHECK(Rsxr_ShouldPatch(*h, true, true, true, true));
    }

    const RsxrHook* psq = Rsxr_FindHook("PSQ1");

    CHECK(psq != nullptr);

    if (psq) {
        g_rsxr_clite = 1;
        CHECK(!Rsxr_ShouldPatch(*psq, true, true, true, true));
        g_rsxr_clite = 0;
        CHECK(Rsxr_ShouldPatch(*psq, true, true, true, true));
        // A failing clite group brings PSQ1 back.
        g_rsxr_clite = 1;
        CHECK(Rsxr_DisableFeature(RsxrFeature::Clite));
        CHECK_EQ(g_rsxr_clite, 0u);
        CHECK(Rsxr_ShouldPatch(*psq, true, true, true, true));
        CHECK(!Rsxr_ShouldPatch(*Rsxr_FindHook("GKC"), true, true, true, true));
    }

    CHECK(std::strcmp(Rsxr_FeatureName(RsxrFeature::Clite), "C-lite") == 0);
    Rsxr_ResetFeatureFailures();
}

TEST(clite_build_count_of_reads_the_players_profile) {
    uint8_t profile[0x40] = {};

    profile[0x38] = 7;
    profile[0x18] = 1;
    uint8_t holder[0x20] = {};

    *reinterpret_cast<uint8_t**>(holder + 8) = profile;
    uint8_t mgr[0x20] = {};

    *reinterpret_cast<uint8_t**>(mgr + 0x10) = holder;  // player 0
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);

    CHECK_EQ(rsxrclite::BuildCountOf(m, 0), 7u);
    CHECK_EQ(rsxrclite::BuildCountOf(m, 1), 6u);  // no holder for player 1
    CHECK_EQ(rsxrclite::BuildCountOf(m, 2), 6u);  // not a player
    profile[0x38] = 8;
    CHECK_EQ(rsxrclite::BuildCountOf(m, 0), 8u);
    profile[0x38] = 9;
    CHECK_EQ(rsxrclite::BuildCountOf(m, 0), 6u);
    profile[0x38] = 7;
    profile[0x18] = 2;
    CHECK_EQ(rsxrclite::BuildCountOf(m, 0), 6u);  // bass profile
    CHECK_EQ(rsxrclite::BuildCountOf(0, 0), 6u);  // no manager yet
}

TEST(clite_and_the_native_ports_fail_together) {
    const uintptr_t sc = g_rsxr_clite, sp = g_rsxr_publish_ports, sn = g_rsxr_native_publish,
                    ss = g_rsxr_native_strings;

    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = 1;
    g_rsxr_publish_ports = 1;
    g_rsxr_native_publish = 1;
    CHECK(Rsxr_DisableFeature(RsxrFeature::Clite));  // clite fails: the ports have nothing that builds the count
    CHECK_EQ(g_rsxr_clite, 0u);
    CHECK_EQ(g_rsxr_publish_ports, 0u);
    CHECK_EQ(g_rsxr_native_publish, 0u);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Publish));
    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = 1;
    g_rsxr_publish_ports = 1;
    g_rsxr_native_publish = 1;
    CHECK(
        Rsxr_DisableFeature(RsxrFeature::Publish));  // the ports fail: a guitar built with 7 strings could not show it
    CHECK_EQ(g_rsxr_clite, 0u);
    CHECK_EQ(g_rsxr_publish_ports, 0u);
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Clite));
    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = 1;
    g_rsxr_publish_ports = 1;
    g_rsxr_native_publish = 1;  // a missing layout takes the ports and clite
    CHECK(Rsxr_DisableFeature(RsxrFeature::Strings));
    CHECK_EQ(g_rsxr_publish_ports, 0u);
    CHECK_EQ(g_rsxr_clite, 0u);
    Rsxr_ResetFeatureFailures();
    g_rsxr_clite = sc;
    g_rsxr_publish_ports = sp;
    g_rsxr_native_publish = sn;
    g_rsxr_native_strings = ss;
}

// Local co-op: each player's guitar is built for that player's arrangement; an unknown player stays at six.
TEST(clite_each_player_is_built_for_its_own_arrangement) {
    using rsxrclite::BuildCountFor;

    CHECK_EQ(BuildCountFor(7, 0), 7u);
    CHECK_EQ(BuildCountFor(8, 0), 8u);
    CHECK_EQ(BuildCountFor(6, 0), 6u);
    CHECK_EQ(BuildCountFor(9, 0), 6u);
    CHECK_EQ(BuildCountFor(7, 1), 7u);
    CHECK_EQ(BuildCountFor(8, 1), 8u);
    CHECK_EQ(BuildCountFor(9, 1), 6u);

    for (unsigned d : {6u, 7u, 8u}) {
        CHECK_EQ(BuildCountFor(d, 2), 6u);
    }
}

TEST(clite_gkc_writes_six_for_a_profile_that_is_not_player_zeros) {
    const uintptr_t sC = g_rsxr_clite, sD = g_rsxr_clite_desired[0], sM = g_rsxr_clite_mgr, sP = g_rsxr_native_pegset;

    g_rsxr_native_pegset = 1;
    uint8_t profile[0x40] = {};
    uint32_t frame[8] = {};  // the RebuildIfProfileChanged frame: [ebp+0Ch] = player
    RsxrMid::Context c{};

    c.esi = uint32_t(reinterpret_cast<uintptr_t>(profile));
    c.ebp = uint32_t(reinterpret_cast<uintptr_t>(frame));
    c.ebx = 0x1000;

    for (unsigned desired : {7u, 8u}) {
        g_rsxr_clite_desired[0] = desired;
        frame[3] = 0;  // player 0: the desired count
        Rsxr_Mid_GKM(c);
        c.jump = 0;
        Rsxr_Mid_GKC(c);
        CHECK_EQ(profile[0x38], uint8_t(desired));
        CHECK_EQ(c.jump, rsxrclite::kGKCResume);
        frame[3] = 1;  // player 1: six strings whatever player 0 asked for; the peg word stays the six-string build's
        Rsxr_Mid_GKM(c);
        Rsxr_Mid_GKC(c);
        CHECK_EQ(profile[0x38], uint8_t(6));
        CHECK_EQ(*reinterpret_cast<uint32_t*>(profile + 0x3A), rsxrclite::PegWord(rsxrclite::PegCount(6, true)));
        frame[3] = 0;  // back to player 0: its next profile is built with the desired count again
        Rsxr_Mid_GKM(c);
        Rsxr_Mid_GKC(c);
        CHECK_EQ(profile[0x38], uint8_t(desired));
    }

    g_rsxr_clite = sC;
    g_rsxr_clite_desired[0] = sD;
    g_rsxr_clite_mgr = sM;
    g_rsxr_native_pegset = sP;
}

TEST(clite_mode_entry_normalises_a_seven_built_second_slot) {
    using rsxrclite::NormaliseSlot1;

    for (const auto& entry : rsxrclite::kRetModeEntries) {
        const uint32_t r = entry.ret;

        CHECK(NormaliseSlot1(r, 7));
        CHECK(NormaliseSlot1(r, 8));
        CHECK(!NormaliseSlot1(r, 6));  // single player / a six-built second slot: nothing, byte-identical
        CHECK(!NormaliseSlot1(r, 4));  // bass profile
    }

    CHECK(!NormaliseSlot1(0x697A24, 7));  // song select never touches slot 1
    CHECK(!NormaliseSlot1(0x7400B3, 7));  // nor do the left-out callers
    CHECK(!NormaliseSlot1(0, 7));
}
