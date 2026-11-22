// The plain-PSARC unit's settings: [psarc] plain (overlay and template). RsxrPlain_StartFromIni takes it from the store
// at startup.
#include "platform/settings.h"
#include "features/psarc/psarc.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

const RsxrSetDef kDefs[] = {
    {"psarc.plain", L"psarc", L"plain", T::Bool, W::Restart, "1", 0, 1, "Plain PSARC loading",
     "Loads unencrypted CDLC packages (the kind chart tools like DotCharter write). If your CDLC enabler already "
     "does this, RSXR leaves it to the enabler. 0: off. Applies after a restart.",
     nullptr},
};
}  // namespace

const RsxrSetTable kRsxrPsarcSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};
