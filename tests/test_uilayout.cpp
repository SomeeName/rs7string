// The overlay's tab contents (platform/uilayout.h): every feature's items, in the order of the old per-tab functions
// (overlay/ui.cpp before the tables).
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "test.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
// One line per item: "S title", "E setting id", "R row name", "O on/off row name", "N" (note).
std::string Layout(RsxrUiTab tab) {
    const RsxrUiItem* items[kRsxrUiMaxTabItems];
    const int n = RsxrUi_TabItems(tab, items, kRsxrUiMaxTabItems);
    std::string s;

    for (int i = 0; i < n && i < kRsxrUiMaxTabItems; ++i) {
        static const char kKinds[] = "SEROn";
        const char k = kKinds[int(items[i]->kind)];

        s += k == 'n' ? std::string("N") : std::string(1, k) + " " + items[i]->text;
        s += "\n";
    }

    return s;
}
}  // namespace

TEST(uilayout_status_features_rows) {
    CHECK(Layout(RsxrUiTab::Status) ==
          "O ramp copy (V6)\nO 7th string line\nO fret extend\nO extra tuning pegs\nO 7-string tuner\nO diagnostics\n");
}

TEST(uilayout_colour_tab) {
    CHECK(Layout(RsxrUiTab::Colour) == "S string 6 colour\nR ramp copy\nR colour mode\nN\n");
}

// The developer keys ([debug] head_*, tuner_autotune, [window] background) are file / pipe only, not in the overlay.
TEST(uilayout_neck_tab) {
    CHECK(Layout(RsxrUiTab::Neck) ==
          "S string spacing\n"
          "E neck.spacing_bass5\nE neck.spacing_bass6\nE neck.spacing_guitar7\nE neck.spacing_guitar8\n"
          "S 7-string headstock model\n"
          "E assets.headstock7_file\nE assets.headstock8_file\nE assets.dir\nE assets.suffix\n");
}

TEST(uilayout_tuner_debug_window_tabs) {
    CHECK(Layout(RsxrUiTab::Tuner) == "S 7-string tuner\nR sessions\n");
    CHECK(Layout(RsxrUiTab::Debug) == "S diagnostics\nE debug.diagnostics\nE debug.song_end_log\n");
    CHECK(Layout(RsxrUiTab::Window) == "S window\nE window.screen\nS overlay\n"
                                       "E overlay.enabled\nE overlay.open\n");
}

// The default view (Player): only what a player would change; everything else is under "Advanced".
TEST(uilayout_player_view) {
    CHECK(Layout(RsxrUiTab::Player) ==
          "S Extra strings\nE color.rsmods_extended_range\nE color.string6_color\nE color.string6_color_cb\n"
          "E color.string7_color\nE color.string7_color_cb\n"
          "S Guitar\nE neck.string7_line\nE assets.headstock7\n"
          "E assets.headstock7_wood\nE chords.wide_misses\nS Tuner and tuning names\nE tuner.lowrange\nE tuner.names\n"
          "E tuner.name_prefix\nE tuner.name_suffix\nS This menu\nE overlay.scale\nE overlay.hotkey\n");
}

// Player settings, and only they, have a plain label and a one-line hint, without key names or developer terms.
TEST(uilayout_player_labels_are_plain) {
    static const char* const kJargon[] = {"ramp", "DXT", "Step", "fncopy", "nslot", "V6",  "atlas", "Rsxr",
                                          "ini",  "_",   "[",    "hook",   "D3D",   "SNG", "native"};

    for (int t = 0; t < kRsxrUiTableCount; ++t) {
        const RsxrUiTable& table = *kRsxrUiTables[t];

        for (int i = 0; i < table.count; ++i) {
            const RsxrUiItem& it = table.items[i];
            const bool setting = it.tab == RsxrUiTab::Player && it.kind == RsxrUiKind::Setting;

            CHECK(it.tab != RsxrUiTab::Player || it.kind == RsxrUiKind::Section || setting);
            const bool section = it.tab == RsxrUiTab::Player && it.kind == RsxrUiKind::Section;

            CHECK((setting || section || !it.label) && (!setting || it.label));
            CHECK(setting == (it.hint != nullptr));
            CHECK(!section || !it.label || *it.label);

            if (!setting || !it.label || !it.hint) {
                continue;
            }

            const bool plain = *it.label && *it.hint && std::strlen(it.hint) <= 100 && !std::strchr(it.hint, '\n');

            if (!plain) {
                std::printf("  hint of '%s' is empty, over 100 characters or has a line break: %s\n", it.label,
                            it.hint);
            }

            CHECK(plain);
            CHECK((it.locked != nullptr) == (it.lockedHint != nullptr));

            if (it.lockedHint) {
                CHECK(std::strlen(it.lockedHint) <= 100 && !std::strchr(it.lockedHint, '\n'));

                for (const char* word : kJargon) {
                    CHECK(!std::strstr(it.lockedHint, word));
                }
            }

            const int k = RsxrSet_Find(it.text);

            CHECK(k >= 0);

            for (const char* word : kJargon) {
                CHECK(!std::strstr(it.label, word) && !std::strstr(it.hint, word));
            }

            if (k >= 0) {
                CHECK(!std::strstr(it.hint, RsxrSet_Def(k).id));
            }
        }
    }
}

// One tab bar: the player tabs come from the Player sections of the tables, and every player setting is in exactly one.
TEST(uilayout_player_tabs) {
    RsxrUiPlayerTab tabs[kRsxrUiMaxPlayerTabs];
    const int n = RsxrUi_PlayerTabs(tabs, kRsxrUiMaxPlayerTabs);
    const char* const kLabels[] = {"Extra strings", "Guitar", "Tuner", "Menu"};
    const char* const kTitles[] = {"Extra strings", "Guitar", "Tuner and tuning names", "This menu"};
    const int kSettings[] = {5, 4, 4, 2};

    CHECK_EQ(n, 4);

    for (int i = 0; i < n && i < 4; ++i) {
        CHECK(std::strcmp(tabs[i].label, kLabels[i]) == 0);
        CHECK(std::strcmp(tabs[i].title, kTitles[i]) == 0);
        CHECK_EQ(tabs[i].count, kSettings[i]);
    }

    // The ranges follow each other without a gap or overlap and together hold every Player setting once.
    const RsxrUiItem* items[kRsxrUiMaxTabItems];
    const int total = RsxrUi_TabItems(RsxrUiTab::Player, items, kRsxrUiMaxTabItems);
    int settings = 0, next = 0;

    for (int i = 0; i < total; ++i) {
        settings += items[i]->kind == RsxrUiKind::Setting ? 1 : 0;
    }

    CHECK(total > 0 && items[0]->kind == RsxrUiKind::Section);  // no Player setting before the first section

    for (int i = 0; i < n && i < 4; ++i) {
        CHECK_EQ(tabs[i].first, next + 1);  // the section item sits right before the range
        CHECK(items[tabs[i].first - 1]->kind == RsxrUiKind::Section);

        for (int k = tabs[i].first; k < tabs[i].first + tabs[i].count; ++k) {
            CHECK(items[k]->kind == RsxrUiKind::Setting);
        }

        next = tabs[i].first + tabs[i].count;
    }

    CHECK_EQ(next, total);
    CHECK_EQ(settings, kSettings[0] + kSettings[1] + kSettings[2] + kSettings[3]);
}

// The window's default height shows a whole player tab: no tab may have more rows than the budget in ui.cpp assumes.
TEST(uilayout_player_tab_fits_the_window) {
    RsxrUiPlayerTab tabs[kRsxrUiMaxPlayerTabs];
    const int n = RsxrUi_PlayerTabs(tabs, kRsxrUiMaxPlayerTabs);

    CHECK(n > 0 && n < kRsxrUiMaxPlayerTabs);

    for (int i = 0; i < n && i < kRsxrUiMaxPlayerTabs; ++i) {
        CHECK(tabs[i].count >= 1 && tabs[i].count <= 8);
        CHECK(tabs[i].locked <= tabs[i].count);
    }
}

TEST(uilayout_settings_exist_once_and_live_rows_have_a_reader) {
    int shown[256] = {};

    for (int t = 0; t < kRsxrUiTableCount; ++t) {
        const RsxrUiTable& table = *kRsxrUiTables[t];

        for (int i = 0; i < table.count; ++i) {
            const RsxrUiItem& it = table.items[i];

            CHECK(it.text && *it.text);

            if (it.kind == RsxrUiKind::Setting) {
                const int k = RsxrSet_Find(it.text);

                CHECK(k >= 0 && k < 256);

                if (k >= 0 && k < 256) {
                    CHECK_EQ(++shown[k], 1);
                    CHECK(std::strcmp(RsxrSet_Def(k).id, it.text) == 0);  // full ids, no bare keys
                }
            }

            CHECK((it.kind == RsxrUiKind::Row || it.kind == RsxrUiKind::OnOff) == (it.live != nullptr));
        }
    }
}

TEST(uilayout_tab_items_stops_at_max) {
    const RsxrUiItem* all[kRsxrUiMaxTabItems] = {};
    const RsxrUiItem* items[2] = {};
    const int n = RsxrUi_TabItems(RsxrUiTab::Window, all, kRsxrUiMaxTabItems);

    CHECK(n > 2);
    CHECK_EQ(RsxrUi_TabItems(RsxrUiTab::Window, items, 2), n);  // the full count, two written
    CHECK(items[0] == all[0] && items[1] == all[1]);
}

TEST(uilayout_no_tab_exceeds_the_item_cap) {
    for (int t = 0; t <= int(RsxrUiTab::Player); ++t) {
        const RsxrUiItem* items[kRsxrUiMaxTabItems];

        CHECK(RsxrUi_TabItems(RsxrUiTab(t), items, kRsxrUiMaxTabItems) <= kRsxrUiMaxTabItems);
    }
}
