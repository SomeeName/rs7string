// The diagnostic log points' setting: [debug] diagnostics (D1-D5) and its startup read.
#include "platform/settings.h"
#include "features/diag/diag.h"
#include "model/groups.h"
#include "platform/config.h"
#include "platform/uilayout.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

RsxrDiagConfig g_diag;

void ApplyDiag(const RsxrSetValue& v) {
    g_rsxr_diag = v.i ? 1 : 0;
}  // R1/R2 logger calls (D2/D3/D5: startup)

void ReadDiag(const wchar_t* iniPath) {
    g_diag = RsxrDiag_ReadIni(iniPath);
}

const RsxrSetDef kDiagDefs[] = {
    {"debug.diagnostics", L"debug", L"diagnostics", T::Bool, W::Live, "0", 0, 1, "Diagnostics (D1-D5)",
     "Extra diagnostic logging for bug reports. Some parts only take effect after a restart.", ApplyDiag},
};
}  // namespace

const RsxrSetTable kRsxrDiagSettings = {kDiagDefs, int(sizeof kDiagDefs / sizeof kDiagDefs[0]), ReadDiag};

namespace {
bool LiveDiag(char* buf, size_t n) {
    if (n) {
        buf[0] = 0;
    }

    return g_rsxr_diag != 0;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    {U::Status, K::OnOff, "diagnostics", LiveDiag},
    {U::Debug, K::Section, "diagnostics"},
    {U::Debug, K::Setting, "debug.diagnostics"},
};
}  // namespace

const RsxrUiTable kRsxrDiagUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

RsxrDiagConfig RsxrDiag_ReadIni(const wchar_t* iniPath) {
    RsxrDiagConfig c;
    if (!iniPath) {
        return c;
    }

    c.diagnosticsRaw = RsxrSet_ReadTableInt(kRsxrDiagSettings, "debug.diagnostics", iniPath);
    c.diagnostics = c.diagnosticsRaw != 0;

    return c;
}

const RsxrDiagConfig& RsxrDiag_Config() {
    return g_diag;
}
