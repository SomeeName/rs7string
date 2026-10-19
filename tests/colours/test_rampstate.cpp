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
        Rs7Log_SetSink(Capture);
    }

    ~Capturing() { Rs7Log_SetSink(nullptr); }
};

// Distinct fake texture pointers: compared, never dereferenced.
void* Tex(uintptr_t i) {
    return reinterpret_cast<void*>(0x10000000u + i * 0x100u);
}

void SetState(void* d3d, Rs7CandState s) {
    Rs7Cand* c = Rs7Cand_Find(d3d);

    CHECK(c != nullptr);
    if (c) {
        c->state = s;
    }
}

bool V6On() {
    return g_rs7_v6[0] == 0.4453125f && g_rs7_v6[1] == 0.9453125f;
}

bool V6Off() {
    return g_rs7_v6[0] == 0.0f && g_rs7_v6[1] == 0.0f;
}
}  // namespace

TEST(v6_is_off_without_candidates) {
    Rs7Cand_ResetForTests();
    CHECK(!Rs7Ramp_V6Rule(true, true));
    Rs7Ramp_UpdateV6(true, true, "test");
    CHECK(V6Off());
}

TEST(v6_turns_on_after_the_first_note_activation) {
    Rs7Cand_ResetForTests();
    Capturing cap;

    Rs7Cand_Add(Tex(1), Rs7Family::Note, "gameplay/note/note_gradient_map.dds");
    Rs7Cand_Add(Tex(2), Rs7Family::Note, "gameplay/tail/note_gradient_map.dds");
    Rs7Ramp_UpdateV6(true, true, "pending");
    CHECK(V6Off());
    SetState(Tex(1), Rs7CandState::Active);  // the other note candidate is still pending: fine
    Rs7Ramp_UpdateV6(true, true, "note active");
    CHECK(V6On());
    Rs7Ramp_UpdateV6(true, true, "again");  // no change: no second log line
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0].rfind("ramp: V6 ON (note active) tid ", 0), size_t(0));
    }
    Rs7Cand_ResetForTests();
    CHECK(V6Off());
}

TEST(additive_only_does_not_turn_v6_on) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Additive, "gameplay/stem/gradient_map_additive.dds");
    SetState(Tex(1), Rs7CandState::Active);
    Rs7Ramp_UpdateV6(true, true, "test");
    CHECK(V6Off());
}

TEST(v6_stays_off_while_any_candidate_is_refused_or_retrying) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");
    Rs7Cand_Add(Tex(2), Rs7Family::Additive, "a");
    SetState(Tex(1), Rs7CandState::Active);
    SetState(Tex(2), Rs7CandState::Refused);
    CHECK(!Rs7Ramp_V6Rule(true, true));
    Rs7Ramp_UpdateV6(true, true, "test");
    CHECK(V6Off());
    SetState(Tex(2), Rs7CandState::Retry);
    CHECK(!Rs7Ramp_V6Rule(true, true));
    SetState(Tex(2), Rs7CandState::Active);
    CHECK(Rs7Ramp_V6Rule(true, true));
    Rs7Cand_ResetForTests();
}

TEST(v6_turns_off_when_a_candidate_fails_later) {
    Rs7Cand_ResetForTests();
    Capturing cap;

    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");
    SetState(Tex(1), Rs7CandState::Active);
    Rs7Ramp_UpdateV6(true, true, "first");
    CHECK(V6On());
    Rs7Cand_Add(Tex(2), Rs7Family::Note, "n2");
    SetState(Tex(2), Rs7CandState::Refused);
    Rs7Ramp_UpdateV6(true, true, "n2 refused");
    CHECK(V6Off());
    CHECK_EQ(Lines().size(), size_t(2));
    if (Lines().size() == 2) {
        CHECK_EQ(Lines()[1].rfind("ramp: V6 OFF (n2 refused) tid ", 0), size_t(0));
    }
    Rs7Cand_ResetForTests();
}

TEST(v6_off_when_disabled_or_device_mismatch) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");
    SetState(Tex(1), Rs7CandState::Active);
    CHECK(Rs7Ramp_V6Rule(true, true));
    CHECK(!Rs7Ramp_V6Rule(false, true));
    CHECK(!Rs7Ramp_V6Rule(true, false));
    Rs7Ramp_UpdateV6(true, true, "on");
    CHECK(V6On());
    Rs7Ramp_UpdateV6(true, false, "other device");
    CHECK(V6Off());
    Rs7Cand_ResetForTests();
}

TEST(cand_add_is_idempotent_and_bounded) {
    Rs7Cand_ResetForTests();
    const int a = Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");

    CHECK_EQ(a, 0);
    CHECK_EQ(Rs7Cand_Add(Tex(1), Rs7Family::Note, "n"), a);

    for (int i = 1; i < kRs7MaxCands; ++i) {
        CHECK_EQ(Rs7Cand_Add(Tex(1 + i), Rs7Family::Additive, "a"), i);
    }

    CHECK_EQ(Rs7Cand_Count(), kRs7MaxCands);
    CHECK_EQ(Rs7Cand_Add(Tex(100), Rs7Family::Note, "17th"), -1);
    CHECK_EQ(Rs7Cand_Count(), kRs7MaxCands);
    CHECK(Rs7Cand_Find(Tex(1))->state == Rs7CandState::Pending);
    CHECK(Rs7Cand_Find(Tex(1))->family == Rs7Family::Note);
    Rs7Cand_ResetForTests();
    CHECK_EQ(Rs7Cand_Count(), 0);
}

TEST(cand_add_truncates_the_path) {
    Rs7Cand_ResetForTests();
    const std::string longPath(300, 'x');

    Rs7Cand_Add(Tex(1), Rs7Family::Note, longPath.c_str());
    Rs7Cand_Add(Tex(2), Rs7Family::Note, nullptr);
    CHECK_EQ(std::strlen(Rs7Cand_Find(Tex(1))->path), sizeof(Rs7Cand::path) - 1);
    CHECK_EQ(Rs7Cand_Find(Tex(2))->path[0], '\0');
    Rs7Cand_ResetForTests();
}

TEST(cand_find_compares_pointers_only) {
    Rs7Cand_ResetForTests();
    CHECK(Rs7Cand_Find(reinterpret_cast<void*>(0x1)) == nullptr);
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");
    CHECK(Rs7Cand_Find(reinterpret_cast<void*>(0x1)) == nullptr);
    CHECK(Rs7Cand_Find(nullptr) == nullptr);
    CHECK(Rs7Cand_Find(Tex(1)) != nullptr);
    Rs7Cand_ResetForTests();
}

TEST(log_stats_line_format) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n");
    SetState(Tex(1), Rs7CandState::Active);
    Rs7Ramp_UpdateV6(true, true, "on");
    Capturing cap;

    Rs7Ramp_LogStats();
    CHECK_EQ(Lines().size(), size_t(1));
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 1 (active 1, refused 0, "
                                         "retry 0, pending 0) v6=ON"));
    }
    Lines().clear();
    Rs7Ramp_CountSwap();
    Rs7Ramp_CountSwap();
    Rs7Ramp_CountHidden();
    Rs7Ramp_CountFault();
    Rs7Cand_Add(Tex(2), Rs7Family::Additive, "a");
    Rs7Ramp_LogStats();
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=2 hidden=1 faults=1 candidates 2 (active 1, refused 0, "
                                         "retry 0, pending 1) v6=ON"));
    }
    Rs7Cand_ResetForTests();
}

// Batch B review: stale entries must leave the table (no foreign match after address reuse, no V6 kept
// off by a dead Refused/Retry entry, no exhaustion by re-walks).
TEST(cand_retire_frees_the_slot_and_unblocks_v6) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n", 0x1000);
    Rs7Cand_Add(Tex(2), Rs7Family::Additive, "a", 0x2000);
    SetState(Tex(1), Rs7CandState::Active);
    SetState(Tex(2), Rs7CandState::Refused);
    CHECK(!Rs7Ramp_V6Rule(true, true));
    Rs7Cand* a = Rs7Cand_Find(Tex(2));

    CHECK(a && a->obj == 0x2000);
    CHECK(!Rs7Cand_Retire(a, Tex(3), false));  // another pointer: not this entry
    CHECK(Rs7Cand_Retire(a, Tex(2), false));
    CHECK(Rs7Cand_Find(Tex(2)) == nullptr);                       // never matches again
    CHECK(Rs7Ramp_V6Rule(true, true));                            // the dead Refused entry no longer blocks
    CHECK(!Rs7Cand_Retire(Rs7Cand_Find(Tex(1)), Tex(1), false));  // Active needs allowActive
    // The retired slot is reused: the table never grows past live entries + 1.
    CHECK_EQ(Rs7Cand_Add(Tex(4), Rs7Family::Note, "n4", 0x4000), 1);
    CHECK(Rs7Cand_Find(Tex(4)) && Rs7Cand_Find(Tex(4))->state == Rs7CandState::Pending);
    CHECK_EQ(Rs7Cand_Count(), 2);
    Rs7Cand_ResetForTests();
}

TEST(cand_claim_is_exclusive_and_blocks_retire) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n", 0x1000);
    Rs7Cand* c = Rs7Cand_Find(Tex(1));

    CHECK(c && Rs7Cand_Claim(c, Tex(1)));
    CHECK(c->state == Rs7CandState::Busy);
    CHECK(!Rs7Cand_Claim(c, Tex(1)));          // already busy
    CHECK(!Rs7Cand_Retire(c, Tex(1), false));  // the worker cannot retire it while busy
    c->state = Rs7CandState::Retry;
    CHECK(Rs7Cand_Claim(c, Tex(1)));  // Retry can be claimed again
    c->state = Rs7CandState::Refused;
    CHECK(!Rs7Cand_Claim(c, Tex(1)));  // Refused and Active are final for Claim
    CHECK(!Rs7Cand_Claim(c, Tex(2)));
    c->state = Rs7CandState::Busy;
    CHECK(Rs7Cand_Retire(c, Tex(1), true));  // the render thread (owner) can
    Rs7Cand_ResetForTests();
}

TEST(cand_retired_and_busy_in_the_stats) {
    Rs7Cand_ResetForTests();
    Rs7Cand_Add(Tex(1), Rs7Family::Note, "n", 0);
    Rs7Cand_Add(Tex(2), Rs7Family::Note, "n", 0);
    Rs7Cand_Claim(Rs7Cand_Find(Tex(1)), Tex(1));          // busy counts as pending
    Rs7Cand_Retire(Rs7Cand_Find(Tex(2)), Tex(2), false);  // retired is not a candidate any more
    CHECK(Rs7Cand_At(1) && Rs7Cand_At(1)->state == Rs7CandState::Retired);
    CHECK(Rs7Cand_At(2) == nullptr && Rs7Cand_At(-1) == nullptr);
    Capturing cap;

    Rs7Ramp_LogStats();
    if (!Lines().empty()) {
        CHECK_EQ(Lines()[0], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 1 (active 0, refused 0, "
                                         "retry 0, pending 1) v6=OFF"));
    }
    Rs7Cand_ResetForTests();
}

TEST(song_setup_logs_the_ramp_stats) {
    Rs7Cand_ResetForTests();
    Capturing cap;
    std::vector<uint8_t> det(0x13B8);  // detector without owner: no matcher
    std::vector<int16_t> tuning(6, 0);
    const Rs7Int16Vec v{tuning.data(), tuning.data() + tuning.size()};

    Rs7_OnSongTuning(nullptr, det.data(), &v);
    CHECK_EQ(Lines().size(), size_t(2));
    if (Lines().size() == 2) {
        CHECK_EQ(Lines()[1], std::string("ramp stats: swaps=0 hidden=0 faults=0 candidates 0 (active 0, refused 0, "
                                         "retry 0, pending 0) v6=OFF"));
    }
}
