// The native GuitarStrings' hooks (sites.json "strings": "native"): looked up by src/registry.cpp (Rsxr_FindHook),
// group Strings; written by the main batch only with the native strings (Rsxr_ShouldPatch reads the rows' strs).
// X_ATTACH is a data reference and stays in the Core group, like every ref.
#include "features/strings/hooks.h"
#include "model/gates.h"
#include "model/groups.h"

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Ref("X_ATTACH", &g_rsxr_fn_attach),
// Mid-hook rows (hooks_entries.cpp): the 16 copied-function entries.
#define RSXR_GSE(id) Rsxr_Mid(#id, &Rsxr_Mid_##id).Gate(kRsxrGateStrings)
    RSXR_GSE(GSFR),
    RSXR_GSE(GSBD),
    RSXR_GSE(GSUL),
    RSXR_GSE(GSLD),
    RSXR_GSE(GSFO),
    RSXR_GSE(GSFI),
    RSXR_GSE(GSXF),
    RSXR_GSE(GSWD),
    RSXR_GSE(GSUP),
    RSXR_GSE(GSRS),
    RSXR_GSE(GSPK),
    RSXR_GSE(GSVB),
    RSXR_GSE(GSIC),
    RSXR_GSE(GSDM),
    RSXR_GSE(GSGL),
    RSXR_GSE(GSHC),
    Rsxr_Mid("GSA1", &Rsxr_Mid_GSA1, nullptr, &g_rsxr_target_GSA1).Gate(kRsxrGateStrings),
    Rsxr_WrapRow(Rsxr_Site("GSC1").Gate(kRsxrGateStrings), kRsxrWrapGSC1, nullptr, &Rsxr_Mid_GSC1),
    Rsxr_WrapRow(Rsxr_Site("GIA1").Gate(kRsxrGateStrings), kRsxrWrapGIA1, &Rsxr_Mid_GIA1Before, &Rsxr_Mid_GIA1),
    Rsxr_Mid("GIP1", &Rsxr_Mid_GIP1, nullptr, &g_rsxr_target_GIP1).Gate(kRsxrGateStrings).After(),
    Rsxr_Mid("GRM1", &Rsxr_Mid_GRM1, &g_rsxr_resume_GRM1).Gate(kRsxrGateStrings),
    Rsxr_Mid("GRM2", &Rsxr_Mid_GRM2, &g_rsxr_resume_GRM2).Gate(kRsxrGateStrings),
    Rsxr_Mid("GRM3", &Rsxr_Mid_GRM3, &g_rsxr_resume_GRM3).Gate(kRsxrGateStrings),
#undef RSXR_GSE
    Rsxr_Mid("GSSS", &Rsxr_Mid_GSSS, &g_rsxr_resume_GSSS, &g_rsxr_target_GSSS).Gate(kRsxrGateStrings),
    Rsxr_Mid("GSSE", &Rsxr_Mid_GSSE, nullptr, &g_rsxr_target_GSSE).Gate(kRsxrGateStrings),
    Rsxr_Mid("GSZ1", &Rsxr_Mid_GSZ1, &g_rsxr_resume_GSZ1).Gate(kRsxrGateStrings),
    Rsxr_Mid("GIZ1", &Rsxr_Mid_GIZ1, &g_rsxr_resume_GIZ1).Gate(kRsxrGateStrings),
};
}  // namespace

extern const RsxrHookTable kRsxrStringsHooks = {Rsxr_Group(RsxrFeature::Strings), kHooks,
                                                sizeof kHooks / sizeof kHooks[0]};
