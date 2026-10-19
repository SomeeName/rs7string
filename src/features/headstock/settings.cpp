// The headstock's settings: [debug] head_*, [neck] headstock_peg and [assets] (overlay and template), and the
// startup read of headstock_peg ([assets] is read by assetload_config.cpp, the head_* keys by hooks_debug.cpp).
#include "platform/settings.h"
#include "features/headstock/headstock.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7HeadstockConfig g_config;

void ApplyHead(const Rs7SetValue&) {
    Rs7Head_SetLive(Rs7Set_Text("debug.head_cull"), Rs7Set_Float("debug.head_stretch"),
                    Rs7Set_Int("debug.head_stretch_mode"));
}

void Read(const wchar_t* iniPath) {
    g_config = Rs7Headstock_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    // [debug]
    {"debug.head_cull", L"debug", L"head_cull", T::Text, W::Live, "", 0, 0, "Headstock cull node",
     "Keep this headstock frame node culled on the 7-string guitar (empty = off).", ApplyHead},
    {"debug.head_stretch", L"debug", L"head_stretch", T::Float, W::Live, "1", 0.5f, 2.0f, "Headstock stretch",
     "Length-only stretch of the headstock about the nut (1 = off).", ApplyHead},
    {"debug.head_stretch_mode", L"debug", L"head_stretch_mode", T::Int, W::Live, "0", 0, 1, "Stretch mode",
     "0 = rotate diag(1,k,1), 1 = also Entity_SetScale(k).", ApplyHead},
    // [neck]
    {"neck.headstock_peg", L"neck", L"headstock_peg", T::Bool, W::Restart, "1", 0, 1, "7th tuning peg",
     "Peg 7 on the 7-string model's Locator_Peg_7 (native PegSet slot 6).", nullptr},
    // [assets]
    {"assets.headstock7", L"assets", L"headstock7", T::Bool, W::Restart, "1", 0, 1, "7-string headstock model",
     "7-string guitars show the 7-string headstock (AL1 + N5): headstock7_file, else dir + suffix.", nullptr},
    {"assets.headstock7_file", L"assets", L"headstock7_file", T::Text, W::Restart, "rs7_inline7.nif", 0, 0,
     "7-string model file",
     "The model for the in-line right-hand frame; a relative name is taken from dir (default: our Facet). none = the "
     "edited-file rule only.",
     nullptr},
    {"assets.headstock7_file_lh", L"assets", L"headstock7_file_lh", T::Text, W::Restart, "rs7_inline7_lh.nif", 0, 0,
     "7-string model file (left-handed)",
     "The model for the in-line left-hand frame (left-handed profiles); a relative name is taken from dir (default: "
     "our Facet, mirrored). none = the edited-file rule only.",
     nullptr},
    {"assets.headstock7_wood", L"assets", L"headstock7_wood", T::Float, W::Restart, "0.6", 0.05f, 1, "Wood brightness",
     "Brightness of the 7-string model's wood colour texture (1 = as built), applied when it is loaded.", nullptr},
    {"assets.dir", L"assets", L"dir", T::Text, W::Restart, "rs7string\\headstock\\", 0, 0, "Edited models folder",
     "<dir>\\<stock base><suffix>.nif for models without headstock7_file. A relative path is taken from the DLL's "
     "folder (the game folder).",
     nullptr},
    {"assets.suffix", L"assets", L"suffix", T::Text, W::Restart, "_7inline", 0, 0, "Edited models suffix",
     "File name suffix of the edited models.", nullptr},
};
}  // namespace

const Rs7SetTable kRs7HeadstockSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LivePeg(char* buf, size_t n) {
    Rs7HeadStats hs{};

    Rs7Head_Stats(&hs);
    std::snprintf(buf, n, "native PegSet: %d built, %d loaded with 7 pegs, %d with peg 7 active%s", hs.pegsets,
                  hs.loaded7, hs.active7, hs.liveOwned ? ", experiments from the overlay" : "");

    return g_rs7_head_on != 0;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

// clang-format off: one item per line, as the tab shows them
const Rs7UiItem kUi[] = {
    Rs7UiPlayer("assets.headstock7", "Seven-string headstock",
                "Shows a headstock with seven tuning pegs on seven-string guitars."),
    Rs7UiPlayer("neck.headstock_peg", "Seventh tuning peg", "Shows the seventh tuning peg on that headstock."),
    Rs7UiPlayer("assets.headstock7_wood", "Headstock wood brightness",
                "Brightness of the seven-string headstock's wood, 1 = brightest."),
    {U::Status, K::OnOff, "7th tuning peg", LivePeg},
    {U::Neck, K::Section, "7-string headstock model"},
    {U::Neck, K::Setting, "assets.headstock7_file"},
    {U::Neck, K::Setting, "assets.dir"},
    {U::Neck, K::Setting, "assets.suffix"},
    {U::Neck, K::Section, "headstock experiments"},
    {U::Neck, K::Setting, "debug.head_cull"},
    {U::Neck, K::Setting, "debug.head_stretch"},
    {U::Neck, K::Setting, "debug.head_stretch_mode"},
};
// clang-format on
}  // namespace

const Rs7UiTable kRs7HeadstockUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const Rs7HeadstockConfig& Rs7Headstock_Config() {
    return g_config;
}

Rs7HeadstockConfig Rs7Headstock_ReadIni(const wchar_t* iniPath) {
    Rs7HeadstockConfig c;
    if (!iniPath) {
        return c;
    }

    c.headstockPegRaw = Rs7Config_ReadInt(L"neck", L"headstock_peg", 1, iniPath);
    c.headstockPeg = c.headstockPegRaw != 0;

    return c;
}
