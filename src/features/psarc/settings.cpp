// The plain-PSARC unit's settings: [psarc] plain (overlay and template). Rs7Plain_StartFromIni takes it from the store
// at startup.
#include "platform/settings.h"
#include "features/psarc/psarc.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

const Rs7SetDef kDefs[] = {
    {"psarc.plain", L"psarc", L"plain", T::Bool, W::Restart, "1", 0, 1, "Plain PSARC loading",
     "Loads unencrypted CDLC packages (the kind chart tools like DotCharter write). If your CDLC enabler already "
     "does this, rs7string leaves it to the enabler. 0: off. Applies after a restart.",
     nullptr},
};
}  // namespace

const Rs7SetTable kRs7PsarcSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};
