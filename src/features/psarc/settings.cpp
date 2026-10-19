// The plain-PSARC unit's settings: [psarc] plain (overlay and template). Rs7Plain_StartFromIni reads it at startup.
#include "platform/settings.h"
#include "features/psarc/psarc.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

const Rs7SetDef kDefs[] = {
    {"psarc.plain", L"psarc", L"plain", T::Bool, W::Restart, "1", 0, 1, "Plain PSARC loading",
     "Unencrypted CDLC packages (plain TOC, SNG flags 1) load with any CDLC enabler. If the enabler already provides "
     "it, rs7string leaves its sites alone. 0: rs7string does not provide it.",
     nullptr},
};
}  // namespace

const Rs7SetTable kRs7PsarcSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};
