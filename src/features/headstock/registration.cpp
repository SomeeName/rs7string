#include "features/headstock/assetload.h"
#include "features/headstock/headstock.h"
#include "features/headstock/hooks.h"
#include "model/groups.h"

// The headstock's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.

namespace {
// A native-PegSet row: written only with the native PegSet (Rs7_ShouldPatch; PSQ1 not with clite, GKC replaces it).
#define RS7_PEG_ROW(ID, STUB, RESUME, TARGET, CALLBACK)                                                                \
    {ID, STUB, RESUME, TARGET, false, false, false, kRs7PegNative, 0, CALLBACK}

// The native 8-slot PegSet (group Pegs): allocation, ctor, load, unload, destructor, free, mesh collection, colours.
const Rs7Hook kPegHooks[] = {
    RS7_PEG_ROW("PSA1", nullptr, nullptr, nullptr, nullptr),
    RS7_PEG_ROW("PSA2", nullptr, &g_rs7_resume_PSA2, nullptr, &Rs7_Mid_PSA2),
    RS7_PEG_ROW("PSD1", nullptr, &g_rs7_resume_PSD1, nullptr, &Rs7_Mid_PSD1),
    RS7_PEG_ROW("PSD2", nullptr, &g_rs7_resume_PSD2, nullptr, &Rs7_Mid_PSD2),
    RS7_PEG_ROW("PSU1", nullptr, &g_rs7_resume_PSU1, &g_rs7_target_PSU1, &Rs7_Mid_PSU1),
    RS7_PEG_ROW("PSU2", nullptr, &g_rs7_resume_PSU2, &g_rs7_target_PSU2, &Rs7_Mid_PSU2),
    RS7_PEG_ROW("PSU3", nullptr, &g_rs7_resume_PSU3, nullptr, &Rs7_Mid_PSU3),
    RS7_PEG_ROW("PSM1", nullptr, &g_rs7_resume_PSM1, &g_rs7_target_PSM1, &Rs7_Mid_PSM1),
    RS7_PEG_ROW("PSM2", nullptr, &g_rs7_resume_PSM2, &g_rs7_target_PSM2, &Rs7_Mid_PSM2),
    RS7_PEG_ROW("PSQ1", nullptr, nullptr, nullptr, nullptr),
    RS7_PEG_ROW("PSC1", nullptr, nullptr, nullptr, &Rs7_Mid_PSC1),
    RS7_PEG_ROW("PSL1", nullptr, &g_rs7_resume_PSL1, nullptr, &Rs7_Mid_PSL1),
    {"PSL2", nullptr, nullptr, &g_rs7_target_PSL2, false, false, false, kRs7PegNative, 0, &Rs7_Mid_PSL2, 0, 0, 0, false,
     Rs7Mid::Mode::Full, 0, Rs7Mid::Call::After},
    RS7_PEG_ROW("PSR1", nullptr, &g_rs7_resume_PSR1, nullptr, &Rs7_Mid_PSR1),
    RS7_PEG_ROW("PSN1", nullptr, &g_rs7_resume_PSN1, nullptr, &Rs7_Mid_PSN1),
    Rs7_WrapRow(RS7_PEG_ROW("PSN2", nullptr, nullptr, nullptr, nullptr), kRs7WrapPSN2, &Rs7_Mid_PSN2Before,
                &Rs7_Mid_PSN2),
};

#undef RS7_PEG_ROW

// The 7-string model and the tuner letters with the refs of the feature (group Core, as before the move).
const Rs7Hook kCoreHooks[] = {
    {"LT1", nullptr, &g_rs7_resume_LT1, nullptr, false, false, false, 0, 0, &Rs7_Mid_LT1},
    {"PGH1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_PGH1},
    {"X_EULER", nullptr, nullptr, &g_rs7_fn_euler, false, true},
    {"X_PEGNAMES", nullptr, nullptr, &g_rs7_pegnames, false, true},
    {"X_PEGSELECT", nullptr, nullptr, &g_rs7_fn_pegselect, false, true},
    {"X_PEGPLACE", nullptr, nullptr, &g_rs7_fn_pegplace, false, true},
    {"X_PEGLAYER", nullptr, nullptr, &g_rs7_fn_peglayer, false, true},
    {"X_FIXSTR", nullptr, nullptr, &g_rs7_fn_fixstr, false, true},
    {"AL1", nullptr, &g_rs7_resume_AL1, nullptr, false, false, false, 0, 0, &Rs7_Mid_AL1},
    {"X_STRPARAMS", nullptr, nullptr, &g_rs7_fn_strparams, false, true},
    {"X_DDSH", nullptr, nullptr, &g_rs7_fn_dds, false, true},
    {"X_NFCTOR", nullptr, nullptr, &g_rs7_fn_nfctor, false, true},
    {"X_NFDTOR", nullptr, nullptr, &g_rs7_fn_nfdtor, false, true},
};
}  // namespace

extern const Rs7HookTable kRs7HeadstockHooks = {Rs7_Group(Rs7Feature::Pegs), kPegHooks,
                                                sizeof kPegHooks / sizeof kPegHooks[0]};
extern const Rs7HookTable kRs7HeadstockCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                    sizeof kCoreHooks / sizeof kCoreHooks[0]};
