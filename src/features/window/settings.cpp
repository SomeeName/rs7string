// The game window's settings: [window] background, screen, console_quickedit_off (overlay and template).
#include "platform/settings.h"
#include "features/window/window.h"
#include "platform/uilayout.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

const Rs7SetDef kWindowDefs[] = {
    {"window.background", L"window", L"background", T::Bool, W::Restart, "0", 0, 1, "Background driving",
     "The game window never activates itself; injected keys via \\\\.\\pipe\\rs7string.", nullptr},
    {"window.screen", L"window", L"screen", T::Int, W::Restart, "0", 0, 16, "Screen",
     "Centre the game window on monitor N (0 = stock placement).", nullptr},
    {"window.console_quickedit_off", L"window", L"console_quickedit_off", T::Bool, W::Restart, "1", 0, 1,
     "Console QuickEdit off",
     "A click into the CDLC enabler's console no longer starts a selection that blocks console writes.", nullptr},
};
}  // namespace

const Rs7SetTable kRs7WindowSettings = {kWindowDefs, int(sizeof kWindowDefs / sizeof kWindowDefs[0]), nullptr};

namespace {
using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    {U::Window, K::Section, "window (test driving)"},
    {U::Window, K::Setting, "window.background"},
    {U::Window, K::Setting, "window.screen"},
};
}  // namespace

const Rs7UiTable kRs7WindowUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
