// The native GuitarStrings' hooks (sites.json "strings": "native"): looked up by src/registry.cpp (Rs7_FindHook),
// group Strings; written by the main batch only with the native strings (Rs7_ShouldPatch reads the rows' strs).
// X_ATTACH is a data reference and stays in the Core group, like every ref.
#include "features/strings/hooks.h"
#include "model/gates.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Ref("X_ATTACH", &g_rs7_fn_attach),
// Mid-hook rows (hooks_entries.cpp): the 16 copied-function entries.
#define RS7_GSE(id) Rs7_Mid(#id, &Rs7_Mid_##id).Gate(kRs7GateStrings)
    RS7_GSE(GSFR),
    RS7_GSE(GSBD),
    RS7_GSE(GSUL),
    RS7_GSE(GSLD),
    RS7_GSE(GSFO),
    RS7_GSE(GSFI),
    RS7_GSE(GSXF),
    RS7_GSE(GSWD),
    RS7_GSE(GSUP),
    RS7_GSE(GSRS),
    RS7_GSE(GSPK),
    RS7_GSE(GSVB),
    RS7_GSE(GSIC),
    RS7_GSE(GSDM),
    RS7_GSE(GSGL),
    RS7_GSE(GSHC),
    Rs7_Mid("GSA1", &Rs7_Mid_GSA1, nullptr, &g_rs7_target_GSA1).Gate(kRs7GateStrings),
    Rs7_WrapRow(Rs7_Site("GSC1").Gate(kRs7GateStrings), kRs7WrapGSC1, nullptr, &Rs7_Mid_GSC1),
    Rs7_WrapRow(Rs7_Site("GIA1").Gate(kRs7GateStrings), kRs7WrapGIA1, &Rs7_Mid_GIA1Before, &Rs7_Mid_GIA1),
    Rs7_Mid("GIP1", &Rs7_Mid_GIP1, nullptr, &g_rs7_target_GIP1).Gate(kRs7GateStrings).After(),
    Rs7_Mid("GRM1", &Rs7_Mid_GRM1, &g_rs7_resume_GRM1).Gate(kRs7GateStrings),
    Rs7_Mid("GRM2", &Rs7_Mid_GRM2, &g_rs7_resume_GRM2).Gate(kRs7GateStrings),
    Rs7_Mid("GRM3", &Rs7_Mid_GRM3, &g_rs7_resume_GRM3).Gate(kRs7GateStrings),
#undef RS7_GSE
    Rs7_Mid("GSSS", &Rs7_Mid_GSSS, &g_rs7_resume_GSSS, &g_rs7_target_GSSS).Gate(kRs7GateStrings),
    Rs7_Mid("GSSE", &Rs7_Mid_GSSE, nullptr, &g_rs7_target_GSSE).Gate(kRs7GateStrings),
    Rs7_Mid("GSZ1", &Rs7_Mid_GSZ1, &g_rs7_resume_GSZ1).Gate(kRs7GateStrings),
    Rs7_Mid("GIZ1", &Rs7_Mid_GIZ1, &g_rs7_resume_GIZ1).Gate(kRs7GateStrings),
};
}  // namespace

extern const Rs7HookTable kRs7StringsHooks = {Rs7_Group(Rs7Feature::Strings), kHooks, sizeof kHooks / sizeof kHooks[0]};
