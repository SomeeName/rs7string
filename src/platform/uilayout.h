#pragma once
#include <cstddef>
#include <cstdint>

// The F9 overlay's tab contents, as data (overlay/ui.cpp draws them, knows no key and no feature).
//
// Each feature owns an RsxrUiTable next to its settings table (features/<name>/settings.cpp): which of its settings
// show, in which tab and order, between which section titles, and its live status rows. kRsxrUiTables
// (src/settings_tables.cpp) lists every table once; a tab shows the items of that tab in list order, then item order.
// The Status tab draws its own sections and the items of tab Status under its "features" title; the Log tab has no
// items.
//
// Player is not one tab: it is the overlay's default view for players. Each Section of the Player items starts one
// player tab (RsxrUiPlayerSection: the title, and the tab's label if the title is too long for a tab), and the Settings
// after it belong to that tab (RsxrUi_PlayerTabs). The player tabs and a last "Advanced" tab form the overlay's one tab
// bar; Advanced holds the other tabs. A Setting in Player has a plain label and a one-line hint of its own
// (RsxrUiPlayer) instead of the table's technical label, help and key name. Everything in the other tabs is advanced.

enum class RsxrUiTab : uint8_t { Status, Colour, Neck, Tuner, Debug, Window, Player };  // the overlay's tab index

enum class RsxrUiKind : uint8_t {
    Section,  // text: the section title (in Player: the title of a player tab, label: the tab's label)
    Setting,  // text: a setting id ("neck.spacing_guitar7"), its control, label and help from the settings table (in
              // Player: label and hint from the item)
    Row,      // text: the row name; live writes the value
    OnOff,    // text: the row name; live returns ON/off and writes the detail (may be empty)
    Note,     // text: a wrapped paragraph
};

// The overlay's limits: at most kRsxrUiMaxTabItems items per tab (more are not drawn; tests/test_uilayout.cpp checks
// every tab), and the live readers' buffer sizes (the row texts of the old per-tab code).
constexpr int kRsxrUiMaxTabItems = 64;
constexpr size_t kRsxrUiRowBuf = 512;    // Row: the value text
constexpr size_t kRsxrUiOnOffBuf = 160;  // OnOff: the detail text

// Render thread, every frame the tab is shown. buf holds n bytes (kRsxrUiRowBuf or kRsxrUiOnOffBuf), always written
// NUL-terminated.
using RsxrUiLive = bool (*)(char* buf, size_t n);

struct RsxrUiItem {
    RsxrUiTab tab;
    RsxrUiKind kind;
    const char* text;
    RsxrUiLive live = nullptr;         // Row, OnOff
    const char* label = nullptr;       // Player Setting: the plain label; Player Section: the tab label (or null)
    const char* hint = nullptr;        // Player Setting: the one-line hint shown below the control
    bool (*locked)() = nullptr;        // Player Setting: true while another setting overrides it (control disabled)
    const char* lockedHint = nullptr;  // Player Setting with locked: the hint shown instead while it is locked
    const char* zeroText = nullptr;    // Player Setting (float): shown instead of the number at exactly 0
};

// A player tab: a section of the Player items. Its tab label is `tabLabel`, or the title if that is null.
constexpr RsxrUiItem RsxrUiPlayerSection(const char* title, const char* tabLabel = nullptr) {
    return {RsxrUiTab::Player, RsxrUiKind::Section, title, nullptr, tabLabel};
}

// A setting in the player view: plain label and hint, no key names or jargon (tests/test_uilayout.cpp checks).
constexpr RsxrUiItem RsxrUiPlayer(const char* id, const char* label, const char* hint) {
    return {RsxrUiTab::Player, RsxrUiKind::Setting, id, nullptr, label, hint};
}

// The same, shown disabled with lockedHint instead of the hint while locked() is true (render thread).
constexpr RsxrUiItem RsxrUiPlayerLocked(const char* id, const char* label, const char* hint, bool (*locked)(),
                                        const char* lockedHint) {
    return {RsxrUiTab::Player, RsxrUiKind::Setting, id, nullptr, label, hint, locked, lockedHint};
}

// The same for a float slider that shows zeroText (for example "Automatic") at 0.
constexpr RsxrUiItem RsxrUiPlayerZero(const char* id, const char* label, const char* hint, const char* zeroText) {
    return {RsxrUiTab::Player, RsxrUiKind::Setting, id, nullptr, label, hint, nullptr, nullptr, zeroText};
}

struct RsxrUiTable {
    const RsxrUiItem* items;
    int count;
};

// One player tab: the Settings of a Player section. first/count index the list RsxrUi_TabItems(Player) returns.
struct RsxrUiPlayerTab {
    const char* title;  // the section title
    const char* label;  // the tab's label ("o tab <label>", lower case, spaces as '-')
    int first = 0;
    int count = 0;   // items after the section until the next one
    int locked = 0;  // of them, Settings that can show their lockedHint (one more line)
};

constexpr int kRsxrUiMaxPlayerTabs = 8;

// The player tabs in display order; writes at most `max`, returns the number of tabs. Player items before the first
// section belong to no tab (tests/test_uilayout.cpp rejects them).
int RsxrUi_PlayerTabs(RsxrUiPlayerTab* out, int max);

// Every table, in order (src/settings_tables.cpp).
extern const RsxrUiTable* const kRsxrUiTables[];
extern const int kRsxrUiTableCount;

// The items of `tab` in display order; writes at most `max` pointers, returns the number of items.
int RsxrUi_TabItems(RsxrUiTab tab, const RsxrUiItem** out, int max);
