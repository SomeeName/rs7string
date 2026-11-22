// The headstock's settings: [debug] head_cull (developer key, file only) and [assets] (overlay and template). [assets]
// is read by assetload_config.cpp, head_cull by hooks_debug.cpp.
#include "platform/settings.h"
#include "features/headstock/headstock.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

void ApplyHead(const RsxrSetValue&) {
    RsxrHead_SetLive(RsxrSet_Text("debug.head_cull"));
}

const RsxrSetDef kDefs[] = {
    // [debug]
    {"debug.head_cull", L"debug", L"head_cull", T::Text, W::Live, "", 0, 0, "Headstock cull node",
     "Keep this headstock frame node culled on the 7-string guitar (empty = off).", ApplyHead},
    // [assets]
    {"assets.headstock7", L"assets", L"headstock7", T::Bool, W::Restart, "1", 0, 1, "Extended headstock models",
     "Extended guitars and basses show the mod's headstock: the file below, else the one in dir. "
     "Applies after a restart.",
     nullptr},
    {"assets.headstock7_file", L"assets", L"headstock7_file", T::Text, W::Restart, "rsxr_inline7.nif", 0, 0,
     "7-string model file",
     "The model for the in-line right-hand frame; a relative name is taken from dir (default: our Facet). "
     "none = only the file in dir. Applies after a restart.",
     nullptr},
    {"assets.headstock7_file_lh", L"assets", L"headstock7_file_lh", T::Text, W::Restart, "rsxr_inline7_lh.nif", 0, 0,
     "7-string model file (left-handed)",
     "The model for the in-line left-hand frame (left-handed profiles); a relative name is taken from dir (default: "
     "our Facet, mirrored). none = only the file in dir. Applies after a restart.",
     nullptr},
    {"assets.headstock8_file", L"assets", L"headstock8_file", T::Text, W::Restart, "rsxr_inline8.nif", 0, 0,
     "8-peg model file",
     "The 8-peg model for the in-line right-hand frame, used instead of headstock7_file; a "
     "relative name is taken from dir. none = only the file in dir. Applies after a restart.",
     nullptr},
    {"assets.headstock8_file_lh", L"assets", L"headstock8_file_lh", T::Text, W::Restart, "rsxr_inline8_lh.nif", 0, 0,
     "8-peg model file (left-handed)",
     "The 8-peg model for the in-line left-hand frame, used instead of headstock7_file_lh. none "
     "= only the file in dir. Applies after a restart.",
     nullptr},
    {"assets.headstock7_wood", L"assets", L"headstock7_wood", T::Float, W::Restart, "0.6", 0.05f, 1, "Wood brightness",
     "Brightness of the 7-string model's wood colour texture (1 = as built). Applies after a "
     "restart.",
     nullptr},
    {"assets.dir", L"assets", L"dir", T::Text, W::Restart, "rsxr\\headstock\\", 0, 0, "Edited models folder",
     "<dir>\\<stock base><suffix>.nif for models without headstock7_file. A relative path is taken from the DLL's "
     "folder (the game folder). Applies after a restart.",
     nullptr},
    {"assets.suffix", L"assets", L"suffix", T::Text, W::Restart, "_7inline", 0, 0, "Edited models suffix",
     "File name suffix of the edited models. Applies after a restart.", nullptr},
};
}  // namespace

const RsxrSetTable kRsxrHeadstockSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};

namespace {
bool LivePeg(char* buf, size_t n) {
    RsxrHeadStats hs{};

    RsxrHead_Stats(&hs);
    std::snprintf(buf, n, "native PegSet: %d built, %d loaded with 7 or 8 pegs, %d with the extra pegs active%s",
                  hs.pegsets, hs.loaded7, hs.active7, hs.liveOwned ? ", head_cull from the overlay" : "");

    return g_rsxr_head_on != 0;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

// clang-format off: one item per line, as the tab shows them
const RsxrUiItem kUi[] = {
    RsxrUiPlayer("assets.headstock7", "Extended headstock",
                "One tuning peg per string on 7- and 8-string guitars and 5- and 6-string basses. Needs a restart."),
    RsxrUiPlayer("assets.headstock7_wood", "Headstock wood brightness",
                "Brightness of the extended headstock's wood, 1 = brightest. Applies after a restart."),
    {U::Status, K::OnOff, "extra tuning pegs", LivePeg},
    {U::Neck, K::Section, "7-string headstock model"},
    {U::Neck, K::Setting, "assets.headstock7_file"},
    {U::Neck, K::Setting, "assets.headstock8_file"},
    {U::Neck, K::Setting, "assets.dir"},
    {U::Neck, K::Setting, "assets.suffix"},
};
// clang-format on
}  // namespace

const RsxrUiTable kRsxrHeadstockUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
