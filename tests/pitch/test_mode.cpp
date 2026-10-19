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
        Put<int>(det, rs7::kDetInstrument, instrument);
        Put<void*>(det, rs7::kDetOwner, input.data());
        Put<void*>(input, rs7::kInputMatcher, matcher.data());
        Put<void*>(matcher, rs7::kMatcherDetector, det.data());
        Put<int>(det, rs7::kDetLowestOpen, 40);
    }

    template <class T>
    static void Put(std::vector<uint8_t>& b, uint32_t off, T v) {
        *reinterpret_cast<T*>(&b[off]) = v;
    }

    const void* DetKey() const { return det.data() + rs7::kDetTuning; }

    const void* MatcherKey() const { return matcher.data() + rs7::kMatcherTuning; }

    int Lowest() const { return *reinterpret_cast<const int*>(&det[rs7::kDetLowestOpen]); }

    void Song(std::vector<int16_t> tuning) {
        const Rs7Int16Vec v{tuning.data(), tuning.data() + tuning.size()};

        Rs7_OnSongTuning(nullptr, det.data(), &v);
    }

    // S3: Play_Detector_SetTuning just ran with this tuning vector (ESI at 0x4E263B).
    void Tuned(std::vector<int16_t> tuning) {
        const Rs7Int16Vec v{tuning.data(), tuning.data() + tuning.size()};

        Rs7_OnDetectorTuned(det.data(), &v);
    }
};
}  // namespace

TEST(seven_entry_guitar_activates_detector_and_matcher) {
    Rs7Store_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, -2});
    CHECK_EQ(Rs7_Base6(p.DetKey()), 33);
    CHECK_EQ(Rs7_Base6(p.MatcherKey()), 33);
}

TEST(six_entries_stay_off) {
    Rs7Store_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), rs7::kNotActive);
}

TEST(bass_never_activates) {
    Rs7Store_ClearAll();
    Player p(2);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), rs7::kNotActive);
}

TEST(six_entry_song_clears_previous_seven) {
    Rs7Store_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), 35);
    p.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), rs7::kNotActive);
    CHECK_EQ(Rs7_Base6(p.MatcherKey()), rs7::kNotActive);
}

TEST(two_players_are_independent) {
    Rs7Store_ClearAll();
    Player a(1), b(1);

    a.Song({0, 0, 0, 0, 0, 0, 0});
    b.Song({0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(a.DetKey()), 35);
    CHECK_EQ(Rs7_Base6(b.DetKey()), rs7::kNotActive);
}

TEST(unknown_key_is_not_active) {
    Rs7Store_ClearAll();
    int x = 0;

    CHECK_EQ(Rs7_Base6(&x), rs7::kNotActive);
}

TEST(unlinked_matcher_is_not_registered) {
    Rs7Store_ClearAll();
    Player p(1);

    Player::Put<void*>(p.matcher, rs7::kMatcherDetector, nullptr);  // matcher does not point back
    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), 35);
    CHECK_EQ(Rs7_Base6(p.MatcherKey()), rs7::kNotActive);
}

TEST(unlinked_matcher_clears_stale_active_entry) {
    Rs7Store_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.MatcherKey()), 35);  // active from a previous song
    Player::Put<void*>(p.matcher, rs7::kMatcherDetector, nullptr);
    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), 35);
    CHECK_EQ(Rs7_Base6(p.MatcherKey()), rs7::kNotActive);
}

TEST(detector_tuned_lowers_lowest_open_only_for_7_entries_and_lower) {
    Rs7Store_ClearAll();
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
    Rs7Store_ClearAll();
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, 0});
    CHECK_EQ(Rs7_Base6(p.DetKey()), 35);  // stale active entry
    p.Tuned({0, 0, 0, 0, 0, 0});
    CHECK_EQ(p.Lowest(), 40);

    // And the reverse: a 7-entry vector acts even without a song setup.
    Rs7Store_ClearAll();
    Player q(1);

    q.Tuned({0, 0, 0, 0, 0, 0, -1});
    CHECK_EQ(q.Lowest(), 34);
}

TEST(detector_tuned_never_touches_bass) {
    Rs7Store_ClearAll();
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

TEST(note_string6_logs_each_fret_once_per_song) {
    Rs7Store_ClearAll();
    NoteLines().clear();
    Rs7Log_SetSink(CaptureNote);
    Player p(1);

    p.Song({0, 0, 0, 0, 0, 0, -1});  // resets the per-song fret set (and logs the song line)
    NoteLines().clear();
    Rs7_NoteString6(5, -1);
    Rs7_NoteString6(5, -1);
    Rs7_NoteString6(-1, -1);  // out of range: ignored
    Rs7_NoteString6(31, -1);
    const std::vector<std::string> first = NoteLines();

    p.Song({0, 0, 0, 0, 0, 0, -1});
    NoteLines().clear();
    Rs7_NoteString6(5, -1);  // new song: logged again
    Rs7Log_SetSink(nullptr);
    const std::string want = "string 6 fret 5: expected MIDI 39 (35 + t6 -1 + fret)";

    CHECK_EQ(first.size(), size_t(1));
    if (!first.empty()) {
        CHECK_EQ(first[0], want);
    }
    CHECK_EQ(NoteLines().size(), size_t(1));
    if (!NoteLines().empty()) {
        CHECK_EQ(NoteLines()[0], want);
    }
}
