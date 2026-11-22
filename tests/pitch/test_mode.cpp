#include "features/pitch/pitch.h"
#include "platform/log.h"
#include "test.h"
#include <string>
#include <vector>

namespace {
// Fake game objects with the layouts of docs/ARCHITECTURE.md (Q2).
struct Player {
    std::vector<uint8_t> det = std::vector<uint8_t>(0x13B8);
    std::vector<uint8_t> input = std::vector<uint8_t>(0x80);
    std::vector<uint8_t> matcher = std::vector<uint8_t>(0x58);

    explicit Player(int instrument) {
        Put<int>(det, rsxr::kDetInstrument, instrument);
        Put<void*>(det, rsxr::kDetOwner, input.data());
        Put<void*>(input, rsxr::kInputMatcher, matcher.data());
        Put<void*>(matcher, rsxr::kMatcherDetector, det.data());
        Put<int>(det, rsxr::kDetLowestOpen, 40);
    }

    template <class T>
    static void Put(std::vector<uint8_t>& b, uint32_t off, T v) {
        *reinterpret_cast<T*>(&b[off]) = v;
    }

    const void* DetKey() const { return det.data() + rsxr::kDetTuning; }

    const void* MatcherKey() const { return matcher.data() + rsxr::kMatcherTuning; }

    int Lowest() const { return *reinterpret_cast<const int*>(&det[rsxr::kDetLowestOpen]); }

    void Song(std::vector<int16_t> tuning) {
        const RsxrInt16Vec v{tuning.data(), tuning.data() + tuning.size()};

        Rsxr_OnSongTuning(nullptr, det.data(), &v);
    }

    // S3: Play_Detector_SetTuning just ran with this tuning vector (ESI at 0x4E263B).
    void Tuned(std::vector<int16_t> tuning) {
        const RsxrInt16Vec v{tuning.data(), tuning.data() + tuning.size()};

        Rsxr_OnDetectorTuned(det.data(), &v);
    }
};
}  // namespace

TEST(seven_entry_guitar_activates_detector_and_matcher) {
    RsxrStore_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, -2});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), 33);
    CHECK_EQ(Rsxr_Base6(p.MatcherKey()), 33);
}

TEST(six_entries_stay_off) {
    RsxrStore_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), rsxr::kNotActive);
}

TEST(bass_never_activates) {
    RsxrStore_ClearAll();
    Player p(2);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), rsxr::kNotActive);
}

TEST(six_entry_song_clears_previous_seven) {
    RsxrStore_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), 35);
    p.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), rsxr::kNotActive);
    CHECK_EQ(Rsxr_Base6(p.MatcherKey()), rsxr::kNotActive);
}

TEST(two_players_are_independent) {
    RsxrStore_ClearAll();
    Player a(1), b(1);

    a.Song({0, 0, 0, 0, 0, 0, 0});
    b.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(a.DetKey()), 35);
    CHECK_EQ(Rsxr_Base6(b.DetKey()), rsxr::kNotActive);
}

TEST(unknown_key_is_not_active) {
    RsxrStore_ClearAll();
    int x = 0;

    CHECK_EQ(Rsxr_Base6(&x), rsxr::kNotActive);
}

TEST(unlinked_matcher_is_not_registered) {
    RsxrStore_ClearAll();
    Player p(1);

    Player::Put<void*>(p.matcher, rsxr::kMatcherDetector, nullptr);  // matcher does not point back
    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), 35);
    CHECK_EQ(Rsxr_Base6(p.MatcherKey()), rsxr::kNotActive);
}

TEST(unlinked_matcher_clears_stale_active_entry) {
    RsxrStore_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.MatcherKey()), 35);  // active from a previous song
    Player::Put<void*>(p.matcher, rsxr::kMatcherDetector, nullptr);
    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), 35);
    CHECK_EQ(Rsxr_Base6(p.MatcherKey()), rsxr::kNotActive);
}

TEST(detector_tuned_lowers_lowest_open_only_for_7_entries_and_lower) {
    RsxrStore_ClearAll();
    Player off(1);

    off.Tuned({0, 0, 0, 0, 0, 0});
    CHECK_EQ(off.Lowest(), 40);

    Player on(1);

    on.Tuned({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(on.Lowest(), 35);

    Player drop(1);

    drop.Tuned({0, 0, 0, 0, 0, 0, -2});
    CHECK_EQ(drop.Lowest(), 33);

    Player high(1);

    high.Tuned({0, 0, 0, 0, 0, 0, 10});  // 35 + 10 = 45 > 40: keep 40
    CHECK_EQ(high.Lowest(), 40);

    Player eight(1);

    eight.Tuned({0, 0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(eight.Lowest(), 40);
}

TEST(detector_tuned_ignores_the_side_store) {
    // Final review F1: a 7-string song leaves the detector's store entry active; a later
    // SetTuning with a 6-entry vector (tuner, Guitarcade: no S4 there) must stay stock.
    RsxrStore_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rsxr_Base6(p.DetKey()), 35);  // stale active entry
    p.Tuned({0, 0, 0, 0, 0, 0});
    CHECK_EQ(p.Lowest(), 40);

    // And the reverse: a 7-entry vector acts even without a song setup.
    RsxrStore_ClearAll();
    Player q(1);

    q.Tuned({0, 0, 0, 0, 0, 0, -1});
    CHECK_EQ(q.Lowest(), 34);
}

TEST(detector_tuned_never_touches_bass) {
    RsxrStore_ClearAll();
    Player p(2);

    p.Tuned({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(p.Lowest(), 40);
}

namespace {
std::vector<std::string>& NoteLines() {
    static std::vector<std::string> v;

    return v;
}

void CaptureNote(const char* msg) {
    NoteLines().push_back(msg);
}
}  // namespace
