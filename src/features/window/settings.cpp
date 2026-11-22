// The game window's settings: [window] background, screen, console_quickedit_off (overlay and template).
#include "platform/settings.h"
#include "features/window/window.h"
#include "platform/uilayout.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

const RsxrSetDef kWindowDefs[] = {
    {"window.background", L"window", L"background", T::Bool, W::Restart, "0", 0, 1, "Background driving",
     "The game window never activates itself; injected keys via \\\\.\\pipe\\rsxr. Applies after a restart.", nullptr},
    {"window.screen", L"window", L"screen", T::Int, W::Restart, "0", 0, 16, "Screen",
     "Centre the game window on monitor N (0 = stock placement). Applies after a restart.", nullptr},
    {"window.console_quickedit_off", L"window", L"console_quickedit_off", T::Bool, W::Restart, "1", 0, 1,
     "Console QuickEdit off",
     "A click into the CDLC enabler's console no longer starts a selection that blocks console writes. "
     "Applies after a restart.",
     nullptr},
};
}  // namespace

const RsxrSetTable kRsxrWindowSettings = {kWindowDefs, int(sizeof kWindowDefs / sizeof kWindowDefs[0]), nullptr};

namespace {
using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    {U::Window, K::Section, "window"},
    {U::Window, K::Setting, "window.screen"},
};
}  // namespace

const RsxrUiTable kRsxrWindowUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
