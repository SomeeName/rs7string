// The overlay's own settings: [overlay] (overlay.h), and their section in the Window tab. RsxrOverlay_Configure takes
// them from the settings store.
#include "overlay/overlay.h"
#include "platform/settings.h"
#include "platform/uilayout.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

void ApplyNothing(const RsxrSetValue&) {}  // read by the overlay itself every frame

const RsxrSetDef kDefs[] = {
    {"overlay.enabled", L"overlay", L"enabled", T::Bool, W::Restart, "1", 0, 1, "Menu enabled",
     "Hooks Present/Reset of the game's device and the window procedure. 1 (default): hidden until the "
     "hotkey shows it (see Open at start). 0: none of it. Applies after a restart.",
     nullptr},
    {"overlay.hotkey", L"overlay", L"hotkey", T::Text, W::Restart, "F9", 0, 0, "Hotkey",
     "Key that shows/hides the menu (F1-F24, A-Z, 0-9, INSERT, DELETE, HOME, END, PGUP, PGDN, PAUSE, SCROLL, "
     "NUMLOCK, OEM_3, or a virtual-key code 0xNN). Applies after a restart.",
     nullptr},
    {"overlay.open", L"overlay", L"open", T::Bool, W::Restart, "0", 0, 1, "Open at start",
     "1: the menu is visible from the first frame. Applies after a restart.", nullptr},
    {"overlay.scale", L"overlay", L"scale", T::Float, W::Live, "0", 0, 4, "Menu size",
     "0 = automatic (window height / 720).", ApplyNothing},
};
}  // namespace

const RsxrSetTable kRsxrOverlaySettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};

namespace {
using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    RsxrUiPlayerSection("This menu", "Menu"),
    RsxrUiPlayerZero("overlay.scale", "Menu size", "Automatic follows the game window's height.", "Automatic"),
    RsxrUiPlayer("overlay.hotkey", "Menu key",
                 "The key that opens and closes this menu, for example F9. Applies after a restart."),
    {U::Window, K::Section, "overlay"},
    {U::Window, K::Setting, "overlay.enabled"},
    {U::Window, K::Setting, "overlay.open"},
};
}  // namespace

const RsxrUiTable kRsxrOverlayUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
