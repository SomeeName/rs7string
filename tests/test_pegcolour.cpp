#include "core/colournative.h"
#include "core/headstock.h"
#include "core/pegcolour.h"
#include "hooks/registry.h"
#include "test.h"
#include <cstring>

namespace {
struct PaletteFixture {
    uintptr_t saved = g_rs7_native_colours;
    alignas(4) uint8_t manager[rs7colour::kSize] = {};
    alignas(4) uint8_t pegs[rs7head::kSize] = {};
    float rgb[2][28][8][3] = {};

    PaletteFixture() {
        g_rs7_native_colours = 1;

        for (unsigned m = 0; m < 2; ++m) {
            for (unsigned st = 0; st < 28; ++st) {
                for (unsigned s = 0; s < 8; ++s) {
                    rgb[m][st][s][0] = float(st) / 28;
                    rgb[m][st][s][1] = float(s) / 8;
                    rgb[m][st][s][2] = float(m);
                    *reinterpret_cast<float**>(manager + rs7colour::PtrOffset(m, st, s)) = rgb[m][st][s];
                }
            }
        }

        *reinterpret_cast<uint16_t*>(pegs + rs7head::kPegCount) = 8;
    }

    ~PaletteFixture() { g_rs7_native_colours = saved; }

    uint8_t* Slot(unsigned s) { return pegs + rs7head::kSlots + s * rs7head::kSlotStride; }

    void Named(unsigned s, uint32_t name) {
        Slot(s)[0] = 1;
        *reinterpret_cast<uint32_t*>(Slot(s) + rs7head::kSlotNamed) = name;
    }
};
}  // namespace

TEST(peg_colour_follows_palette_edits_after_selection_without_changing_outline_animation) {
    PaletteFixture f;

    f.Named(5, 7);
    const float old[3] = {0.5f, 0, 1};
    const float next[3] = {0.1f, 0.7f, 0.3f};

    std::memcpy(f.Slot(5) + rs7head::kSlotRgb, old, 12);
    *reinterpret_cast<float*>(f.Slot(5) + 0x20) = 0.25f;
    *reinterpret_cast<float*>(f.Slot(5) + 0x24) = 0.75f;
    std::memcpy(f.rgb[0][4][5], next, 12);
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 1u);
    CHECK(std::memcmp(f.Slot(5) + rs7head::kSlotRgb, next, 12) == 0);
    CHECK_EQ(*reinterpret_cast<float*>(f.Slot(5) + 0x20), 0.25f);
    CHECK_EQ(*reinterpret_cast<float*>(f.Slot(5) + 0x24), 0.75f);
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 0u);
}

TEST(peg_colour_uses_each_players_current_mode_and_extended_slot) {
    PaletteFixture f;

    f.Named(6, 9);
    *reinterpret_cast<uint32_t*>(f.manager + 0x34C) = 1;
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 1), 1u);
    CHECK(std::memcmp(f.Slot(6) + rs7head::kSlotRgb, f.rgb[1][4][7], 12) == 0);
    *reinterpret_cast<uint32_t*>(f.manager + 0x34C) = 0;
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 1), 1u);
    CHECK(std::memcmp(f.Slot(6) + rs7head::kSlotRgb, f.rgb[0][4][7], 12) == 0);
}

TEST(peg_colour_preserves_reset_and_success_meanings_and_ignores_unknown_names) {
    PaletteFixture f;

    f.Named(0, 0);
    f.Named(1, 1);
    f.Named(2, 100);
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 2u);
    CHECK(std::memcmp(f.Slot(0) + rs7head::kSlotRgb, f.rgb[0][6][0], 12) == 0);
    CHECK(std::memcmp(f.Slot(1) + rs7head::kSlotRgb, f.rgb[0][5][0], 12) == 0);
    const float zero[3] = {};

    CHECK(std::memcmp(f.Slot(2) + rs7head::kSlotRgb, zero, 12) == 0);
}

TEST(peg_colour_rejects_invalid_manager_player_count_and_mode) {
    PaletteFixture f;

    f.Named(0, 2);
    CHECK_EQ(rs7pegcolour::Refresh(nullptr, f.manager, 0), 0u);
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, nullptr, 0), 0u);
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 2), 0u);
    *reinterpret_cast<uint32_t*>(f.manager + 0x348) = 2;
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 0u);
    *reinterpret_cast<uint32_t*>(f.manager + 0x348) = 0;
    *reinterpret_cast<uint16_t*>(f.pegs + rs7head::kPegCount) = 100;
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 0u);
    g_rs7_native_colours = 0;
    CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 0), 0u);
}

TEST(peg_colour_extra_recalculates_brightness_after_delayed_rsmods_palette) {
    PaletteFixture f;
    float saved[6];

    std::memcpy(saved, g_rs7_fx_rgb, sizeof saved);
    const float chosen[6] = {0, 0.8f, 0.6f, 0.9f, 0.3f, 0};

    std::memcpy(g_rs7_fx_rgb, chosen, sizeof chosen);
    f.Named(6, 8);

    for (unsigned mode = 0; mode < 2; ++mode) {
        *reinterpret_cast<uint32_t*>(f.manager + 0x34C) = mode;
        const float baseline[3] = {1, 0, 0};
        const float delayed[3] = {0, 0.25f, 0};

        std::memcpy(f.rgb[mode][1][0], baseline, 12);
        std::memcpy(f.rgb[mode][4][0], delayed, 12);
        CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 1), 1u);

        for (unsigned c = 0; c < 3; ++c) {
            CHECK_EQ(reinterpret_cast<float*>(f.Slot(6) + rs7head::kSlotRgb)[c], chosen[mode * 3 + c] * 0.25f);
        }

        CHECK_EQ(rs7pegcolour::Refresh(f.pegs, f.manager, 1), 0u);
    }

    std::memcpy(g_rs7_fx_rgb, saved, sizeof saved);
}
