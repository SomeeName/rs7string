// The fret bars' overlay status row. They have no ini keys since 1.0 ([neck] fret_extend / fret_halflen were removed:
// the bars always span the extra strings, with the default half length).
#include "features/frets/frets.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
bool LiveFrets(char* buf, size_t n) {
    RsxrFretsStats fr{};

    RsxrFrets_Stats(&fr);
    std::snprintf(buf, n, "%d guitar(s) applied, L = %.3f", fr.applied, double(g_rsxr_fret_halflen));

    return g_rsxr_frets_on != 0;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    {U::Status, K::OnOff, "fret extend", LiveFrets},
};
}  // namespace

const RsxrUiTable kRsxrFretUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
