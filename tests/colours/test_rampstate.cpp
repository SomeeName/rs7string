#include "features/colours/colours.h"
#include "features/pitch/pitch.h"
#include "hook_rules.h"
#include "platform/log.h"
#include "test.h"
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<std::string>& Lines() {
    static std::vector<std::string> v;

    return v;
}

void Capture(const char* msg) {
    Lines().push_back(msg);
}

struct Capturing {
    Capturing() {
        Lines().clear();
        RsxrLog_SetSink(Capture);
    }

    ~Capturing() { RsxrLog_SetSink(nullptr); }
};

// Distinct fake texture pointers: compared, never dereferenced.
void* Tex(uintptr_t i) {
    return reinterpret_cast<void*>(0x10000000u + i * 0x100u);
}

void SetState(void* d3d, RsxrCandState s) {
    RsxrCand* c = RsxrCand_Find(d3d);

    CHECK(c != nullptr);
    if (c) {
        c->state = s;
    }
}

bool V6On() {
    return g_rsxr_v6[0] == 0.4453125f && g_rsxr_v6[1] == 0.9453125f;
}

bool V6Off() {
    return g_rsxr_v6[0] == 0.0f && g_rsxr_v6[1] == 0.0f;
}
}  // namespace

TEST(v6_is_off_without_candidates) {
    RsxrCand_ResetForTests();
    CHECK(!RsxrRamp_V6Rule(true, true));
    RsxrRamp_UpdateV6(true, true, "test");
    CHECK(V6Off());
}

TEST(v6_turns_on_after_the_first_note_activation) {
    RsxrCand_ResetForTests();
    Capturing cap;

    RsxrCand_Add(Tex(1), RsxrFamily::Note, "gameplay/note/note_gradient_map.dds");
    RsxrCand_Add(Tex(2), RsxrFamily::Note, "gameplay/tail/note_gradient_map.dds");
    RsxrRamp_UpdateV6(true, true, "pending");
    CHECK(V6Off());
    SetState(Tex(1), RsxrCandState::Active);  // the other note candidate is still pending: fine
    RsxrRamp_UpdateV6(true, true, "note active");
    CHECK(V6On());
    RsxrRamp_UpdateV6(true, true, "again");  // no change: no second log line
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0].rfind("ramp: V6 ON (note active) tid ", 0), size_t(0));
    }
    RsxrCand_ResetForTests();
    CHECK(V6Off());
}

TEST(additive_only_does_not_turn_v6_on) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Additive, "gameplay/stem/gradient_map_additive.dds");
    SetState(Tex(1), RsxrCandState::Active);
    RsxrRamp_UpdateV6(true, true, "test");
    CHECK(V6Off());
}

TEST(v6_stays_off_while_any_candidate_is_refused_or_retrying) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    RsxrCand_Add(Tex(2), RsxrFamily::Additive, "a");
    SetState(Tex(1), RsxrCandState::Active);
    SetState(Tex(2), RsxrCandState::Refused);
    CHECK(!RsxrRamp_V6Rule(true, true));
    RsxrRamp_UpdateV6(true, true, "test");
    CHECK(V6Off());
    SetState(Tex(2), RsxrCandState::Retry);
    CHECK(!RsxrRamp_V6Rule(true, true));
    SetState(Tex(2), RsxrCandState::Active);
    CHECK(RsxrRamp_V6Rule(true, true));
    RsxrCand_ResetForTests();
}

TEST(v6_ignores_embedded_candidates_whatever_their_state) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    RsxrCand_Add(Tex(2), RsxrFamily::Embedded, "(NIF texture)");
    SetState(Tex(1), RsxrCandState::Active);
    SetState(Tex(2), RsxrCandState::Refused);  // a 256x128 NIF texture that is no ramp
    CHECK(RsxrRamp_V6Rule(true, true));
    SetState(Tex(1), RsxrCandState::Pending);
    SetState(Tex(2), RsxrCandState::Active);  // an embedded ramp alone does not turn V6 on
    CHECK(!RsxrRamp_V6Rule(true, true));
    RsxrCand_ResetForTests();
}

TEST(v6_turns_off_when_a_candidate_fails_later) {
    RsxrCand_ResetForTests();
    Capturing cap;

    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    SetState(Tex(1), RsxrCandState::Active);
    RsxrRamp_UpdateV6(true, true, "first");
    CHECK(V6On());
    RsxrCand_Add(Tex(2), RsxrFamily::Note, "n2");
    SetState(Tex(2), RsxrCandState::Refused);
    RsxrRamp_UpdateV6(true, true, "n2 refused");
    CHECK(V6Off());
    CHECK_EQ(Lines().size(), size_t(2));
    if (Lines().size() == 2) {
        CHECK_EQ(Lines()[1].rfind("ramp: V6 OFF (n2 refused) tid ", 0), size_t(0));
    }
    RsxrCand_ResetForTests();
}

TEST(v6_off_when_disabled_or_device_mismatch) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    SetState(Tex(1), RsxrCandState::Active);
    CHECK(RsxrRamp_V6Rule(true, true));
    CHECK(!RsxrRamp_V6Rule(false, true));
    CHECK(!RsxrRamp_V6Rule(true, false));
    RsxrRamp_UpdateV6(true, true, "on");
    CHECK(V6On());
    RsxrRamp_UpdateV6(true, false, "other device");
    CHECK(V6Off());
    RsxrCand_ResetForTests();
}

TEST(cand_add_is_idempotent_and_bounded) {
    RsxrCand_ResetForTests();
    const int a = RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");

    CHECK_EQ(a, 0);
    CHECK_EQ(RsxrCand_Add(Tex(1), RsxrFamily::Note, "n"), a);

    for (int i = 1; i < kRsxrMaxCands; ++i) {
        CHECK_EQ(RsxrCand_Add(Tex(1 + i), RsxrFamily::Additive, "a"), i);
    }

    CHECK_EQ(RsxrCand_Count(), kRsxrMaxCands);
    CHECK_EQ(RsxrCand_Add(Tex(100), RsxrFamily::Note, "one too many"), -1);
    CHECK_EQ(RsxrCand_Count(), kRsxrMaxCands);
    CHECK(RsxrCand_Find(Tex(1))->state == RsxrCandState::Pending);
    CHECK(RsxrCand_Find(Tex(1))->family == RsxrFamily::Note);
    RsxrCand_ResetForTests();
    CHECK_EQ(RsxrCand_Count(), 0);
}

TEST(cand_add_truncates_the_path) {
    RsxrCand_ResetForTests();
    const std::string longPath(300, 'x');

    RsxrCand_Add(Tex(1), RsxrFamily::Note, longPath.c_str());
    RsxrCand_Add(Tex(2), RsxrFamily::Note, nullptr);
    CHECK_EQ(std::strlen(RsxrCand_Find(Tex(1))->path), sizeof(RsxrCand::path) - 1);
    CHECK_EQ(RsxrCand_Find(Tex(2))->path[0], '\0');
    RsxrCand_ResetForTests();
}

TEST(cand_find_compares_pointers_only) {
    RsxrCand_ResetForTests();
    CHECK(RsxrCand_Find(reinterpret_cast<void*>(0x1)) == nullptr);
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    CHECK(RsxrCand_Find(reinterpret_cast<void*>(0x1)) == nullptr);
    CHECK(RsxrCand_Find(nullptr) == nullptr);
    CHECK(RsxrCand_Find(Tex(1)) != nullptr);
    RsxrCand_ResetForTests();
}

TEST(log_stats_line_format) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n");
    SetState(Tex(1), RsxrCandState::Active);
    RsxrRamp_UpdateV6(true, true, "on");
    Capturing cap;

    RsxrRamp_LogStats();
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 1 (active 1, refused 0, "
                                         "retry 0, pending 0) v6=ON"));
    }
    Lines().clear();
    RsxrRamp_CountSwap();
    RsxrRamp_CountSwap();
    RsxrRamp_CountHidden();
    RsxrRamp_CountFault();
    RsxrCand_Add(Tex(2), RsxrFamily::Additive, "a");
    RsxrRamp_LogStats();
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=2 hidden=1 faults=1 candidates 2 (active 1, refused 0, "
                                         "retry 0, pending 1) v6=ON"));
    }
    RsxrCand_ResetForTests();
}

// Batch B review: stale entries must leave the table (no foreign match after address reuse, no V6 kept
// off by a dead Refused/Retry entry, no exhaustion by re-walks).
TEST(cand_retire_frees_the_slot_and_unblocks_v6) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n", 0x1000);
    RsxrCand_Add(Tex(2), RsxrFamily::Additive, "a", 0x2000);
    SetState(Tex(1), RsxrCandState::Active);
    SetState(Tex(2), RsxrCandState::Refused);
    CHECK(!RsxrRamp_V6Rule(true, true));
    RsxrCand* a = RsxrCand_Find(Tex(2));

    CHECK(a && a->obj == 0x2000);
    CHECK(!RsxrCand_Retire(a, Tex(3), false));  // another pointer: not this entry
    CHECK(RsxrCand_Retire(a, Tex(2), false));
    CHECK(RsxrCand_Find(Tex(2)) == nullptr);                        // never matches again
    CHECK(RsxrRamp_V6Rule(true, true));                             // the dead Refused entry no longer blocks
    CHECK(!RsxrCand_Retire(RsxrCand_Find(Tex(1)), Tex(1), false));  // Active needs allowActive
    // The retired slot is reused: the table never grows past live entries + 1.
    CHECK_EQ(RsxrCand_Add(Tex(4), RsxrFamily::Note, "n4", 0x4000), 1);
    CHECK(RsxrCand_Find(Tex(4)) && RsxrCand_Find(Tex(4))->state == RsxrCandState::Pending);
    CHECK_EQ(RsxrCand_Count(), 2);
    RsxrCand_ResetForTests();
}

TEST(cand_claim_is_exclusive_and_blocks_retire) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n", 0x1000);
    RsxrCand* c = RsxrCand_Find(Tex(1));

    CHECK(c && RsxrCand_Claim(c, Tex(1)));
    CHECK(c->state == RsxrCandState::Busy);
    CHECK(!RsxrCand_Claim(c, Tex(1)));          // already busy
    CHECK(!RsxrCand_Retire(c, Tex(1), false));  // the worker cannot retire it while busy
    c->state = RsxrCandState::Retry;
    CHECK(RsxrCand_Claim(c, Tex(1)));  // Retry can be claimed again
    c->state = RsxrCandState::Refused;
    CHECK(!RsxrCand_Claim(c, Tex(1)));  // Refused and Active are final for Claim
    CHECK(!RsxrCand_Claim(c, Tex(2)));
    c->state = RsxrCandState::Busy;
    CHECK(RsxrCand_Retire(c, Tex(1), true));  // the render thread (owner) can
    RsxrCand_ResetForTests();
}

TEST(cand_retired_and_busy_in_the_stats) {
    RsxrCand_ResetForTests();
    RsxrCand_Add(Tex(1), RsxrFamily::Note, "n", 0);
    RsxrCand_Add(Tex(2), RsxrFamily::Note, "n", 0);
    RsxrCand_Claim(RsxrCand_Find(Tex(1)), Tex(1));          // busy counts as pending
    RsxrCand_Retire(RsxrCand_Find(Tex(2)), Tex(2), false);  // retired is not a candidate any more
    CHECK(RsxrCand_At(1) && RsxrCand_At(1)->state == RsxrCandState::Retired);
    CHECK(RsxrCand_At(2) == nullptr && RsxrCand_At(-1) == nullptr);
    Capturing cap;

    RsxrRamp_LogStats();
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 1 (active 0, refused 0, "
                                         "retry 0, pending 1) v6=OFF"));
    }
    RsxrCand_ResetForTests();
}

TEST(song_setup_logs_the_ramp_stats) {
    RsxrCand_ResetForTests();
    Capturing cap;
    std::vector<uint8_t> det(0x13B8);  // detector without owner: no matcher
    std::vector<int16_t> tuning(6, 0);
    const RsxrInt16Vec v{tuning.data(), tuning.data() + tuning.size()};

    Rsxr_OnSongTuning(nullptr, det.data(), &v);
    // The song setup line, then the ramp stats. Before them a pitch floor line may come, only for a detector address
    // not seen before (the heap may hand out an earlier test's buffer address), so it is not counted.
    const size_t n = Lines().size();

    CHECK(n == 2 || n == 3);

    if (n >= 2) {
        CHECK_EQ(Lines()[n - 2].rfind("song setup: ", 0), size_t(0));
        CHECK_EQ(Lines()[n - 1], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 0 (active 0, refused 0, "
                                             "retry 0, pending 0) v6=OFF"));
    }
}
