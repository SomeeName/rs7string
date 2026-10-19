// The overlay's own settings: [overlay] (overlay.h), and their section in the Window tab. Rs7Overlay_Configure takes
// them from the settings store.
#include "overlay/overlay.h"
#include "platform/settings.h"
#include "platform/uilayout.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

void ApplyNothing(const Rs7SetValue&) {}  // read by the overlay itself every frame

const Rs7SetDef kDefs[] = {
    {"overlay.enabled", L"overlay", L"enabled", T::Bool, W::Restart, "1", 0, 1, "Overlay enabled",
     "Hooks Present/Reset of the game's device and the window procedure. 1 (default): hidden until the "
     "hotkey shows it (see Open at start). 0: none of it.",
     nullptr},
    {"overlay.hotkey", L"overlay", L"hotkey", T::Text, W::Restart, "F9", 0, 0, "Hotkey",
     "Key that shows/hides the overlay (F1-F24, INSERT, HOME, END, PAUSE, SCROLL, A-Z, 0-9, or 0xNN).", nullptr},
    {"overlay.open", L"overlay", L"open", T::Bool, W::Restart, "0", 0, 1, "Open at start",
     "1: the overlay is visible from the first frame.", nullptr},
    {"overlay.scale", L"overlay", L"scale", T::Float, W::Live, "0", 0, 4, "UI scale",
     "0 = automatic (window height / 720).", ApplyNothing},
};
}  // namespace

const Rs7SetTable kRs7OverlaySettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};

namespace {
using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    Rs7UiPlayerSection("This menu", "Menu"),
    Rs7UiPlayerZero("overlay.scale", "Menu size", "Automatic follows the game window's height.", "Automatic"),
    Rs7UiPlayer("overlay.hotkey", "Menu key", "The key that opens and closes this menu, for example F9."),
    {U::Window, K::Section, "overlay"},
    {U::Window, K::Setting, "overlay.enabled"},
    {U::Window, K::Setting, "overlay.open"},
};
}  // namespace

const Rs7UiTable kRs7OverlayUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
