// The 7-string look's settings: [neck] string7_line / string7_v (overlay and template) and their startup read.
#include "platform/settings.h"
#include "features/string7/string7.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7String7Config g_config;

void ApplyAtlasRow(const Rs7SetValue& v) {
    g_rs7_s7_v = v.f[0];
}  // N5 writes it every shown frame

void StartupAtlasRow(Rs7SetValue& v) {
    v.f[0] = g_config.string7V;
}

void Read(const wchar_t* iniPath) {
    g_config = Rs7String7_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"neck.string7_line", L"neck", L"string7_line", T::Bool, W::Restart, "1", 0, 1, "7th string line",
     "The 7th string line (native GuitarStrings); pools are sized at the guitar load.", nullptr},
    {"neck.string7_v", L"neck", L"string7_v", T::Float, W::Live, "0.4", 0, 0.9f, "7th string atlas row",
     "0.4..0.9 = guitar strings 0..5, 0.0..0.3 = bass strings.", ApplyAtlasRow, StartupAtlasRow},
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
    Rs7UiPlayer("neck.string7_line", "Seventh string on the neck", "Draws the extra string across the neck."),
    {U::Status, K::OnOff, "7th string line", LiveLine},
    {U::Neck, K::Section, "7th string line"},
    {U::Neck, K::Setting, "neck.string7_v"},
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

    bool tooLong = false;

    c.string7LineRaw = Rs7Config_ReadInt(L"neck", L"string7_line", 1, iniPath);
    c.string7Line = c.string7LineRaw != 0;
    c.string7VRaw = Rs7Config_ReadString(L"neck", L"string7_v", iniPath, &tooLong);

    if (!c.string7VRaw.empty()) {
        float v = 0.0f;

        c.string7VValid = !tooLong && Rs7_ParseAtlasRow(c.string7VRaw.c_str(), &v);
        if (c.string7VValid) {
            c.string7V = v;
        }
    }

    return c;
}
