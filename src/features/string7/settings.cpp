// The 7-string look's settings: [neck] string7_line (overlay and template) and their startup read.
#include "platform/settings.h"
#include "features/string7/string7.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

RsxrString7Config g_config;

void Read(const wchar_t* iniPath) {
    g_config = RsxrString7_ReadIni(iniPath);
}

const RsxrSetDef kDefs[] = {
    {"neck.string7_line", L"neck", L"string7_line", T::Bool, W::Restart, "1", 0, 1, "7th string line",
     "Shows the string lines of the extra strings. Applies after a restart.", nullptr},
};
}  // namespace

const RsxrSetTable kRsxrString7Settings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LiveLine(char* buf, size_t n) {
    RsxrS7Stats s7{};

    RsxrS7_Stats(&s7);
    std::snprintf(buf, n, "%d GuitarStrings, %d guitar(s) marked 7-string", s7.guitars, s7.sevenGuitars);

    return g_rsxr_s7_on != 0;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    RsxrUiPlayerSection("Guitar"),
    RsxrUiPlayer("neck.string7_line", "Extra strings on the neck",
                 "Draws the extra strings across the neck. Applies after a restart."),
    {U::Status, K::OnOff, "7th string line", LiveLine},
};
}  // namespace

const RsxrUiTable kRsxrString7Ui = {kUi, int(sizeof kUi / sizeof kUi[0])};

const RsxrString7Config& RsxrString7_Config() {
    return g_config;
}

RsxrString7Config RsxrString7_ReadIni(const wchar_t* iniPath) {
    RsxrString7Config c;
    if (!iniPath) {
        return c;
    }

    c.string7LineRaw = RsxrSet_ReadTableInt(kRsxrString7Settings, "neck.string7_line", iniPath);
    c.string7Line = c.string7LineRaw != 0;

    return c;
}
