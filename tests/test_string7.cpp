#include "core/string7.h"
#include "log.h"
#include "test.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
// The authored dimmed colours are 8-bit and hand-rounded: they match the relation to 2/255.
bool Near(float a, float b) {
    return std::fabs(a - b) <= 2.0f / 255.0f;
}

float B(int v) {
    return float(v) / 255.0f;
}

// Stock state 1 -> state 2 pairs of strings 0-5, both colour modes (GameColorManager in the menu
// snapshot, docs/ARCHITECTURE.md "Highlight of the 7th string").
const int kPairs[12][2] = {
    {0xFF4F5A, 0x4C171B}, {0xE2C102, 0x4C4100}, {0x1DACF9, 0x09364C}, {0xFF9216, 0x4C2A06},
    {0x3FCC0C, 0x174C04}, {0xC825ED, 0x3F0C4C}, {0xC12A2A, 0x4C1010}, {0xA3F400, 0x344C00},
    {0x19ACFC, 0x07344C}, {0xDB7F41, 0x4C2C16}, {0x00C68E, 0x004C36}, {0x7648A8, 0x35204C},
};

void Rgb(int v, float out[3]) {
    out[0] = B((v >> 16) & 0xFF);
    out[1] = B((v >> 8) & 0xFF);
    out[2] = B(v & 0xFF);
}

// The stock back-fill 0x55CD30..0x55CE3D for one string and level, transcribed literally.
void StockBackfill(std::vector<uint8_t>& sec, int count, uint32_t level, uint8_t bit) {
    int i = count - 1;

    while (i > 0) {
        uint8_t& cur = sec[size_t(i) * 0x58 + 0x34 + level];
        uint8_t& prev = sec[size_t(i - 1) * 0x58 + 0x34 + level];

        if ((cur & bit) && !(prev & bit)) {
            prev = uint8_t(prev | bit);
            i -= 1;
        }

        i -= 1;
    }
}
}  // namespace

TEST(s7_dim_colour_matches_the_stock_palette_relation) {
    for (const auto& p : kPairs) {
        float lit[3], want[3], got[3];

        Rgb(p[0], lit);
        Rgb(p[1], want);
        Rs7S7_DimColour(lit, rs7s7::kDefaultDim, got);

        for (int i = 0; i < 3; ++i) {
            CHECK(Near(got[i], want[i]));
        }
    }
}

TEST(s7_dim_colour_of_the_default_grey_and_black) {
    float grey[3] = {B(0xC8), B(0xC8), B(0xC8)}, out[3];

    Rs7S7_DimColour(grey, rs7s7::kDefaultDim, out);

    for (float v : out) {
        CHECK(Near(v, B(0x4C)));
    }

    float black[3] = {0.0f, 0.0f, 0.0f};

    Rs7S7_DimColour(black, rs7s7::kDefaultDim, out);

    for (float v : out) {
        CHECK_EQ(v, 0.0f);
    }
}

TEST(s7_backfill_equals_the_stock_loop) {
    // Every mask pattern over 7 sections, bit 6 and a stock bit, two levels.
    for (uint32_t level : {0u, 31u}) {
        for (uint8_t bit : {uint8_t(0x40), uint8_t(0x01)}) {
            for (int pattern = 0; pattern < 128; ++pattern) {
                const int n = 7;
                std::vector<uint8_t> a(size_t(n) * 0x58, 0), b;

                for (int i = 0; i < n; ++i) {
                    if (pattern & (1 << i)) {
                        a[size_t(i) * 0x58 + 0x34 + level] = uint8_t(bit | 0x02);
                    }
                }

                b = a;
                StockBackfill(a, n, level, bit);
                int added = -1;
                const int used = Rs7S7_BackfillBit(b.data(), n, level, bit, &added);

                CHECK(a == b);
                int lit = 0, before = 0;

                for (int i = 0; i < n; ++i) {
                    lit += (b[size_t(i) * 0x58 + 0x34 + level] & bit) ? 1 : 0;
                    before += (pattern >> i) & 1;
                }

                CHECK_EQ(used, lit);
                CHECK_EQ(added, lit - before);
            }
        }
    }
}

TEST(s7_backfill_rejects_bad_input) {
    std::vector<uint8_t> a(0x58 * 2, 0xFF);
    int added = -1;

    CHECK_EQ(Rs7S7_BackfillBit(nullptr, 2, 0, 0x40, &added), 0);
    CHECK_EQ(added, 0);
    CHECK_EQ(Rs7S7_BackfillBit(a.data(), 2, 32, 0x40, &added), 0);  // stock: level < 32 only
    CHECK_EQ(Rs7S7_BackfillBit(a.data(), 0, 0, 0x40, nullptr), 0);
}

TEST(s7_mask_backfill_reads_the_song_sections) {
    // song +0F4h/+0F8h = sections begin/end; string 6 in section 2 only -> section 1 lit early.
    std::vector<uint8_t> sec(0x58 * 3, 0);

    sec[2 * 0x58 + 0x34 + 5] = 0x40;
    uint8_t song[0x100] = {};
    uint8_t* b = sec.data();
    uint8_t* e = sec.data() + sec.size();

    std::memcpy(song + 0xF4, &b, 4);
    std::memcpy(song + 0xF8, &e, 4);
    const uintptr_t on = g_rs7_s7_on;

    g_rs7_s7_on = 0;
    Rs7_S7MaskBackfill(song, 5);
    CHECK_EQ(sec[1 * 0x58 + 0x34 + 5], 0);  // switch off: untouched
    g_rs7_s7_on = 1;
    Rs7_S7MaskBackfill(song, 4);
    CHECK_EQ(sec[1 * 0x58 + 0x34 + 4], 0);  // other level: nothing to fill
    Rs7_S7MaskBackfill(song, 5);
    CHECK_EQ(sec[1 * 0x58 + 0x34 + 5], 0x40);
    CHECK_EQ(sec[0 * 0x58 + 0x34 + 5], 0);
    g_rs7_s7_on = on;
}

// ---- N5/N6 placement (run 7 regression: S7 built in a song frame stayed at the pool's parking
// position, because Rnd_Guitar_ApplyTransforms - N6 - does not run every song frame) ----
TEST(s7_glow_colour_is_the_string6_colour_at_full_value) {
    const float grey[3] = {200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f};
    float out[3];

    Rs7S7_GlowColour(grey, out);
    CHECK(out[0] == 1.0f && out[1] == 1.0f && out[2] == 1.0f);
    const float c[3] = {0.5f, 0.25f, 0.0f};

    Rs7S7_GlowColour(c, out);
    CHECK(out[0] == 1.0f && out[1] == 0.5f && out[2] == 0.0f);
    const float black[3] = {0.0f, 0.0f, 0.0f};

    Rs7S7_GlowColour(black, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
}

TEST(s7_targets_need_a_native_guitarstrings) {
    int gs = 0;

    CHECK(Rs7_S7DimTarget(nullptr) == nullptr);
    CHECK(Rs7_S7DimTarget(&gs) == nullptr);  // not a native GuitarStrings (1.0: no S7 object exists)
    CHECK(Rs7_S7BendTarget(&gs, 0, 3, 0) == nullptr);
    CHECK(Rs7_S7Pluck(&gs, 3, 3) == 0);
}

// ---- issue #6 / #27: the 7-string look is on from the S4 arm (song load) until the guitar scene is gone ----
TEST(s7_visibility_rule) {
    using rs7s7::Disarm;

    rs7s7::g_fadeHoldMs = 0;  // the fade-out counts as finished at once
    auto vis = [](bool seven, bool& armed, bool song, bool ended, bool out, bool outDone, bool unseen) {
        return rs7s7::Rs7S7_Visibility(seven, armed, song, ended, out, outDone, unseen);
    };
    bool armed = true;
    // Armed before the song: shown (the intro fade-in comes before string 0's +70h).
    auto v = vis(true, armed, false, false, false, false, false);

    CHECK(v.want);
    CHECK(armed);
    // Song running: shown; a fade-out or a gap during the song does not end the arm.
    v = vis(true, armed, true, false, true, true, true);
    CHECK(v.want);
    CHECK(armed);
    CHECK(v.disarmed == Disarm::None);
    // Song end with the strings still up (outro, issue #27): the arm holds, still shown.
    v = vis(true, armed, false, true, false, false, false);
    CHECK(v.want);
    CHECK(armed);
    CHECK(v.disarmed == Disarm::None);
    v = vis(true, armed, false, false, false, false, false);
    CHECK(v.want);
    // The stock strings start fading out after the song: still on (string 6 fades with them) ...
    v = vis(true, armed, false, false, true, false, false);
    CHECK(v.want);
    CHECK(armed);
    // ... the fade has finished: the arm ends, hidden from then on.
    v = vis(true, armed, false, false, true, true, false);
    CHECK(!v.want);
    CHECK(!armed);
    CHECK(v.disarmed == Disarm::FadedOut);
    v = vis(true, armed, false, false, true, false, false);
    CHECK(!v.want);
    CHECK(v.disarmed == Disarm::None);
    // A restart without a new S4: shown again while the song runs.
    CHECK(vis(true, armed, true, false, false, false, false).want);
    // Song end while the strings were already faded out (quit from a faded pause): hidden at once.
    armed = true;
    v = vis(true, armed, false, true, true, false, false);
    CHECK(!v.want);
    CHECK(!armed);
    CHECK(v.disarmed == Disarm::FadedOut);
    // After the song the guitar was not updated for a while (results/menus, reused object): arm ends.
    armed = true;
    v = vis(true, armed, false, false, false, false, true);
    CHECK(!v.want);
    CHECK(!armed);
    CHECK(v.disarmed == Disarm::Unseen);
    // Left during the intro: the stock strings fade out before the song started -> arm ends.
    armed = true;
    v = vis(true, armed, false, false, true, true, false);
    CHECK(!v.want);
    CHECK(!armed);
    CHECK(v.disarmed == Disarm::FadedOut);
    // Not 7-string: never shown, whatever the arm or the song.
    armed = true;
    CHECK(!vis(false, armed, true, false, false, false, false).want);
    CHECK(!vis(false, armed, false, false, false, false, false).want);
}

namespace {
std::vector<std::string> g_lines;

void Sink(const char* msg) {
    g_lines.emplace_back(msg);
}

bool Logged(const char* text) {
    for (const std::string& l : g_lines) {
        if (l.find(text) != std::string::npos) {
            return true;
        }
    }

    return false;
}

template <class T>
void Put(uint8_t* base, uint32_t off, T v) {
    std::memcpy(base + off, &v, sizeof v);
}
}  // namespace

// N5 on a guitar outside the native publication path: the S4 arm and the song / fade edges are followed and logged.
TEST(s7_n5_follows_the_arm_from_song_setup_to_the_fade_out) {
    rs7s7::g_fadeHoldMs = 0;  // no clock in the tests: a fade-out counts as finished at once
    alignas(16) static uint8_t guitar[0x40], gs[0x40], obj0[0x110], info[0x40];

    std::memset(guitar, 0, sizeof guitar);
    std::memset(gs, 0, sizeof gs);
    std::memset(obj0, 0, sizeof obj0);
    std::memset(info, 0, sizeof info);
    Put(gs, 4, static_cast<uint8_t*>(obj0));
    Put(guitar, 0xC, static_cast<uint8_t*>(gs));
    Put(guitar, 0x24, static_cast<uint8_t*>(info));
    info[0xB] = 6;
    g_lines.clear();
    Rs7Log_SetSink(Sink);
    Rs7S7_ResetLog();
    Rs7_S7Record(gs);
    Rs7S7Stats st{};

    Rs7S7_Stats(&st);
    CHECK(st.guitars >= 1);
    Rs7S7_SetGuitarMode(guitar, true);  // S4 (song load)
    Rs7S7_Stats(&st);
    CHECK(st.sevenGuitars >= 1);
    Rs7_S7Update(guitar);
    obj0[0x70] = 1;  // song start
    Rs7_S7Update(guitar);
    obj0[0x70] = 0;  // song end: the outro keeps the arm (issue #27)
    Rs7_S7Update(guitar);
    CHECK(Logged("string7: edge guitar"));
    gs[0x38] = 1;  // the strings fade out after the outro: the arm ends
    Rs7_S7Update(guitar);
    CHECK(Logged("the stock strings faded out while no song runs"));
    Rs7S7_SetGuitarMode(guitar, false);
    Rs7_S7Free(gs);
    Rs7S7_Stats(&st);
    Rs7Log_SetSink(nullptr);
}
