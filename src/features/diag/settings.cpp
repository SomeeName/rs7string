// The diagnostic log points' setting: [debug] diagnostics (D1-D5) and its startup read.
#include "platform/settings.h"
#include "features/diag/diag.h"
#include "model/groups.h"
#include "platform/config.h"
#include "platform/uilayout.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7DiagConfig g_diag;

void ApplyDiag(const Rs7SetValue& v) {
    g_rs7_diag = v.i ? 1 : 0;
}  // R1/R2 logger calls (D2/D3/D5: startup)

void ReadDiag(const wchar_t* iniPath) {
    g_diag = Rs7Diag_ReadIni(iniPath);
}

const Rs7SetDef kDiagDefs[] = {
    {"debug.diagnostics", L"debug", L"diagnostics", T::Bool, W::Live, "0", 0, 1, "Diagnostics (D1-D5)",
     "Live: the R1/R2 loggers (D1/D4). D2/D3/D5 are written only when it is 1 at startup.", ApplyDiag},
};
}  // namespace

const Rs7SetTable kRs7DiagSettings = {kDiagDefs, int(sizeof kDiagDefs / sizeof kDiagDefs[0]), ReadDiag};

namespace {
bool LiveDiag(char* buf, size_t n) {
    if (n) {
        buf[0] = 0;
    }

    return g_rs7_diag != 0;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    {U::Status, K::OnOff, "diagnostics", LiveDiag},
    {U::Debug, K::Section, "diagnostics"},
    {U::Debug, K::Setting, "debug.diagnostics"},
};
}  // namespace

const Rs7UiTable kRs7DiagUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

Rs7DiagConfig Rs7Diag_ReadIni(const wchar_t* iniPath) {
    Rs7DiagConfig c;
    if (!iniPath) {
        return c;
    }

    c.diagnosticsRaw = Rs7Config_ReadInt(L"debug", L"diagnostics", 0, iniPath);
    c.diagnostics = c.diagnosticsRaw != 0;

    return c;
}

const Rs7DiagConfig& Rs7Diag_Config() {
    return g_diag;
}
