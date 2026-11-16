// The fret bars' overlay status row. They have no ini keys since 1.0 ([neck] fret_extend / fret_halflen were removed:
// the bars always span the extra strings, with the default half length).
#include "features/frets/frets.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
bool LiveFrets(char* buf, size_t n) {
    Rs7FretsStats fr{};

    Rs7Frets_Stats(&fr);
    std::snprintf(buf, n, "%d guitar(s) applied, L = %.3f", fr.applied, double(g_rs7_fret_halflen));

    return g_rs7_frets_on != 0;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    {U::Status, K::OnOff, "fret extend", LiveFrets},
};
}  // namespace

const Rs7UiTable kRs7FretUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
