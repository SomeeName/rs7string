// The overlay's tab contents (platform/uilayout.h): every feature's items, in the order of the old per-tab functions
// (overlay/ui.cpp before the tables).
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "test.h"
#include <cstring>
#include <string>

namespace {
// One line per item: "S title", "E setting id", "R row name", "O on/off row name", "N" (note).
std::string Layout(Rs7UiTab tab) {
    const Rs7UiItem* items[kRs7UiMaxTabItems];
    const int n = Rs7Ui_TabItems(tab, items, kRs7UiMaxTabItems);
    std::string s;

    for (int i = 0; i < n && i < kRs7UiMaxTabItems; ++i) {
        static const char kKinds[] = "SEROn";
        const char k = kKinds[int(items[i]->kind)];

        s += k == 'n' ? std::string("N") : std::string(1, k) + " " + items[i]->text;
        s += "\n";
    }

    return s;
}
}  // namespace

TEST(uilayout_status_features_rows) {
    CHECK(Layout(Rs7UiTab::Status) ==
          "O ramp copy (V6)\nO 7th string line\nO fret extend\nO 7th tuning peg\nO 7-string tuner\nO diagnostics\n");
}

TEST(uilayout_colour_tab) {
    CHECK(Layout(Rs7UiTab::Colour) ==
          "S string 6 colour\nR ramp copy\nR colour mode\nN\nS ramp copy (Step 2)\nE color.ramp_swap\n"
          "E color.allow_inexact\n");
}

TEST(uilayout_neck_tab) {
    CHECK(Layout(Rs7UiTab::Neck) ==
          "S 7th string line\nE neck.string7_v\nS fret wires\nE neck.fret_halflen\nS 7-string headstock model\n"
          "E assets.headstock7_file\nE assets.dir\nE assets.suffix\nS headstock experiments\nE debug.head_cull\n"
          "E debug.head_stretch\nE debug.head_stretch_mode\n");
}

TEST(uilayout_tuner_debug_window_tabs) {
    CHECK(Layout(Rs7UiTab::Tuner) == "S 7-string tuner\nE debug.tuner_autotune\nR sessions\n");
    CHECK(Layout(Rs7UiTab::Debug) == "S diagnostics\nE debug.diagnostics\nE debug.song_end_log\n");
    CHECK(Layout(Rs7UiTab::Window) == "S window (test driving)\nE window.background\nE window.screen\nS overlay\n"
                                      "E overlay.enabled\nE overlay.open\n");
}

// The default view (Player): only what a player would change; everything else is under "Advanced".
TEST(uilayout_player_view) {
    CHECK(Layout(Rs7UiTab::Player) ==
          "S Seventh string\nE color.string6_color\nE color.string6_color_cb\nE color.rsmods_extended_range\n"
          "S Guitar\nE neck.string7_line\nE neck.fret_extend\nE assets.headstock7\nE neck.headstock_peg\n"
          "E assets.headstock7_wood\nS Tuner and tuning names\nE tuner.lowrange\nE tuner.names\n"
          "E tuner.name_prefix\nE tuner.name_suffix\nS This menu\nE overlay.scale\nE overlay.hotkey\n");
}

// Player settings, and only they, have a plain label and a one-line hint, without key names or developer terms.
TEST(uilayout_player_labels_are_plain) {
    static const char* const kJargon[] = {"ramp", "DXT", "Step", "fncopy", "nslot", "V6",  "atlas", "Rs7",
                                          "ini",  "_",   "[",    "hook",   "D3D",   "SNG", "native"};

    for (int t = 0; t < kRs7UiTableCount; ++t) {
        const Rs7UiTable& table = *kRs7UiTables[t];

        for (int i = 0; i < table.count; ++i) {
            const Rs7UiItem& it = table.items[i];
            const bool setting = it.tab == Rs7UiTab::Player && it.kind == Rs7UiKind::Setting;

            CHECK(it.tab != Rs7UiTab::Player || it.kind == Rs7UiKind::Section || setting);
            const bool section = it.tab == Rs7UiTab::Player && it.kind == Rs7UiKind::Section;

            CHECK((setting || section || !it.label) && (!setting || it.label));
            CHECK(setting == (it.hint != nullptr));
            CHECK(!section || !it.label || *it.label);

            if (!setting || !it.label || !it.hint) {
                continue;
            }

            CHECK(*it.label && *it.hint && std::strlen(it.hint) <= 100 && !std::strchr(it.hint, '\n'));
            CHECK((it.locked != nullptr) == (it.lockedHint != nullptr));

            if (it.lockedHint) {
                CHECK(std::strlen(it.lockedHint) <= 100 && !std::strchr(it.lockedHint, '\n'));

                for (const char* word : kJargon) {
                    CHECK(!std::strstr(it.lockedHint, word));
                }
            }

            const int k = Rs7Set_Find(it.text);

            CHECK(k >= 0);

            for (const char* word : kJargon) {
                CHECK(!std::strstr(it.label, word) && !std::strstr(it.hint, word));
            }

            if (k >= 0) {
                CHECK(!std::strstr(it.hint, Rs7Set_Def(k).id));
            }
        }
    }
}

// One tab bar: the player tabs come from the Player sections of the tables, and every player setting is in exactly one.
TEST(uilayout_player_tabs) {
    Rs7UiPlayerTab tabs[kRs7UiMaxPlayerTabs];
    const int n = Rs7Ui_PlayerTabs(tabs, kRs7UiMaxPlayerTabs);
    const char* const kLabels[] = {"Seventh string", "Guitar", "Tuner", "Menu"};
    const char* const kTitles[] = {"Seventh string", "Guitar", "Tuner and tuning names", "This menu"};
    const int kSettings[] = {3, 5, 4, 2};

    CHECK_EQ(n, 4);

    for (int i = 0; i < n && i < 4; ++i) {
        CHECK(std::strcmp(tabs[i].label, kLabels[i]) == 0);
        CHECK(std::strcmp(tabs[i].title, kTitles[i]) == 0);
        CHECK_EQ(tabs[i].count, kSettings[i]);
    }

    // The ranges follow each other without a gap or overlap and together hold every Player setting once.
    const Rs7UiItem* items[kRs7UiMaxTabItems];
    const int total = Rs7Ui_TabItems(Rs7UiTab::Player, items, kRs7UiMaxTabItems);
    int settings = 0, next = 0;

    for (int i = 0; i < total; ++i) {
        settings += items[i]->kind == Rs7UiKind::Setting ? 1 : 0;
    }

    CHECK(total > 0 && items[0]->kind == Rs7UiKind::Section);  // no Player setting before the first section

    for (int i = 0; i < n && i < 4; ++i) {
        CHECK_EQ(tabs[i].first, next + 1);  // the section item sits right before the range
        CHECK(items[tabs[i].first - 1]->kind == Rs7UiKind::Section);

        for (int k = tabs[i].first; k < tabs[i].first + tabs[i].count; ++k) {
            CHECK(items[k]->kind == Rs7UiKind::Setting);
        }

        next = tabs[i].first + tabs[i].count;
    }

    CHECK_EQ(next, total);
    CHECK_EQ(settings, kSettings[0] + kSettings[1] + kSettings[2] + kSettings[3]);
}

// The window's default height shows a whole player tab: no tab may have more rows than the budget in ui.cpp assumes.
TEST(uilayout_player_tab_fits_the_window) {
    Rs7UiPlayerTab tabs[kRs7UiMaxPlayerTabs];
    const int n = Rs7Ui_PlayerTabs(tabs, kRs7UiMaxPlayerTabs);

    CHECK(n > 0 && n < kRs7UiMaxPlayerTabs);

    for (int i = 0; i < n && i < kRs7UiMaxPlayerTabs; ++i) {
        CHECK(tabs[i].count >= 1 && tabs[i].count <= 8);
        CHECK(tabs[i].locked <= tabs[i].count);
    }
}

TEST(uilayout_settings_exist_once_and_live_rows_have_a_reader) {
    int shown[256] = {};

    for (int t = 0; t < kRs7UiTableCount; ++t) {
        const Rs7UiTable& table = *kRs7UiTables[t];

        for (int i = 0; i < table.count; ++i) {
            const Rs7UiItem& it = table.items[i];

            CHECK(it.text && *it.text);

            if (it.kind == Rs7UiKind::Setting) {
                const int k = Rs7Set_Find(it.text);

                CHECK(k >= 0 && k < 256);

                if (k >= 0 && k < 256) {
                    CHECK_EQ(++shown[k], 1);
                    CHECK(std::strcmp(Rs7Set_Def(k).id, it.text) == 0);  // full ids, no bare keys
                }
            }

            CHECK((it.kind == Rs7UiKind::Row || it.kind == Rs7UiKind::OnOff) == (it.live != nullptr));
        }
    }
}

TEST(uilayout_tab_items_stops_at_max) {
    const Rs7UiItem* all[kRs7UiMaxTabItems] = {};
    const Rs7UiItem* items[2] = {};
    const int n = Rs7Ui_TabItems(Rs7UiTab::Window, all, kRs7UiMaxTabItems);

    CHECK(n > 2);
    CHECK_EQ(Rs7Ui_TabItems(Rs7UiTab::Window, items, 2), n);  // the full count, two written
    CHECK(items[0] == all[0] && items[1] == all[1]);
}

TEST(uilayout_no_tab_exceeds_the_item_cap) {
    for (int t = 0; t <= int(Rs7UiTab::Player); ++t) {
        const Rs7UiItem* items[kRs7UiMaxTabItems];

        CHECK(Rs7Ui_TabItems(Rs7UiTab(t), items, kRs7UiMaxTabItems) <= kRs7UiMaxTabItems);
    }
}
