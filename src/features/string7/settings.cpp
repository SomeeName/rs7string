// The 7-string look's settings: [neck] string7_line (overlay and template) and their startup read.
#include "platform/settings.h"
#include "features/string7/string7.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7String7Config g_config;

void Read(const wchar_t* iniPath) {
    g_config = Rs7String7_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"neck.string7_line", L"neck", L"string7_line", T::Bool, W::Restart, "1", 0, 1, "7th string line",
     "Shows the string lines of the extra strings. Applies after a restart.", nullptr},
};
}  // namespace

const Rs7SetTable kRs7String7Settings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LiveLine(char* buf, size_t n) {
    Rs7S7Stats s7{};

    Rs7S7_Stats(&s7);
    std::snprintf(buf, n, "%d GuitarStrings, %d guitar(s) marked 7-string", s7.guitars, s7.sevenGuitars);

    return g_rs7_s7_on != 0;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    Rs7UiPlayerSection("Guitar"),
    Rs7UiPlayer("neck.string7_line", "Extra strings on the neck",
                "Draws the extra strings across the neck. Applies after a restart."),
    {U::Status, K::OnOff, "7th string line", LiveLine},
};
}  // namespace

const Rs7UiTable kRs7String7Ui = {kUi, int(sizeof kUi / sizeof kUi[0])};

const Rs7String7Config& Rs7String7_Config() {
    return g_config;
}

Rs7String7Config Rs7String7_ReadIni(const wchar_t* iniPath) {
    Rs7String7Config c;
    if (!iniPath) {
        return c;
    }

    c.string7LineRaw = Rs7Set_ReadTableInt(kRs7String7Settings, "neck.string7_line", iniPath);
    c.string7Line = c.string7LineRaw != 0;

    return c;
}
