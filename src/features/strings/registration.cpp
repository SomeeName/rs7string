// The native GuitarStrings' hooks (sites.json "strings": "native"): looked up by src/registry.cpp (Rs7_FindHook),
// group Strings; written by the main batch only with the native strings (Rs7_ShouldPatch reads the rows' strs).
// X_ATTACH is a data reference and stays in the Core group, like every ref.
#include "features/strings/hooks.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    {"X_ATTACH", nullptr, nullptr, &g_rs7_fn_attach, false, true},
// Mid-hook rows (hooks_entries.cpp): the 16 copied-function entries.
#define RS7_GSE(id) {#id, nullptr, nullptr, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_##id}
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
    {"GSA1", nullptr, nullptr, &g_rs7_target_GSA1, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSA1},
    Rs7_WrapRow({"GSC1", nullptr, nullptr, nullptr, false, false, false, 0, kRs7StrNative}, kRs7WrapGSC1, nullptr,
                &Rs7_Mid_GSC1),
    Rs7_WrapRow({"GIA1", nullptr, nullptr, nullptr, false, false, false, 0, kRs7StrNative}, kRs7WrapGIA1,
                &Rs7_Mid_GIA1Before, &Rs7_Mid_GIA1),
    {"GRM1", nullptr, &g_rs7_resume_GRM1, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GRM1},
    {"GRM2", nullptr, &g_rs7_resume_GRM2, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GRM2},
    {"GRM3", nullptr, &g_rs7_resume_GRM3, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GRM3},
#undef RS7_GSE
    {"GSSS", nullptr, &g_rs7_resume_GSSS, &g_rs7_target_GSSS, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSSS},
    {"GSSE", nullptr, nullptr, &g_rs7_target_GSSE, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSSE},
    {"GSZ1", nullptr, &g_rs7_resume_GSZ1, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GSZ1},
    {"GIZ1", nullptr, &g_rs7_resume_GIZ1, nullptr, false, false, false, 0, kRs7StrNative, &Rs7_Mid_GIZ1},
};
}  // namespace

extern const Rs7HookTable kRs7StringsHooks = {Rs7_Group(Rs7Feature::Strings), kHooks, sizeof kHooks / sizeof kHooks[0]};
