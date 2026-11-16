#include "features/headstock/assetload.h"
#include "features/headstock/headstock.h"
#include "features/headstock/hooks.h"
#include "model/gates.h"
#include "model/groups.h"

// The headstock's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch.

namespace {
// A native-PegSet row: written only with the native PegSet (Rs7_ShouldPatch; PSQ1 not with clite, GKC replaces it).

// The native 8-slot PegSet (group Pegs): allocation, ctor, load, unload, destructor, free, mesh collection, colours.
const Rs7Hook kPegHooks[] = {
    Rs7_Site("PSA1").Gate(kRs7GatePegs),
    Rs7_Mid("PSA2", &Rs7_Mid_PSA2, &g_rs7_resume_PSA2).Gate(kRs7GatePegs),
    Rs7_Mid("PSD1", &Rs7_Mid_PSD1, &g_rs7_resume_PSD1).Gate(kRs7GatePegs),
    Rs7_Mid("PSD2", &Rs7_Mid_PSD2, &g_rs7_resume_PSD2).Gate(kRs7GatePegs),
    Rs7_Mid("PSU1", &Rs7_Mid_PSU1, &g_rs7_resume_PSU1, &g_rs7_target_PSU1).Gate(kRs7GatePegs),
    Rs7_Mid("PSU2", &Rs7_Mid_PSU2, &g_rs7_resume_PSU2, &g_rs7_target_PSU2).Gate(kRs7GatePegs),
    Rs7_Mid("PSU3", &Rs7_Mid_PSU3, &g_rs7_resume_PSU3).Gate(kRs7GatePegs),
    Rs7_Mid("PSM1", &Rs7_Mid_PSM1, &g_rs7_resume_PSM1, &g_rs7_target_PSM1).Gate(kRs7GatePegs),
    Rs7_Mid("PSM2", &Rs7_Mid_PSM2, &g_rs7_resume_PSM2, &g_rs7_target_PSM2).Gate(kRs7GatePegs),
    Rs7_Site("PSQ1").Gate(kRs7GateNoClite).Gate(kRs7GatePegs),
    Rs7_Mid("PSC1", &Rs7_Mid_PSC1).Gate(kRs7GatePegs),
    Rs7_Mid("PSL1", &Rs7_Mid_PSL1, &g_rs7_resume_PSL1).Gate(kRs7GatePegs),
    Rs7_Mid("PSL2", &Rs7_Mid_PSL2, nullptr, &g_rs7_target_PSL2).Gate(kRs7GatePegs).After(),
    Rs7_Mid("PSR1", &Rs7_Mid_PSR1, &g_rs7_resume_PSR1).Gate(kRs7GatePegs),
    Rs7_Mid("PSN1", &Rs7_Mid_PSN1, &g_rs7_resume_PSN1).Gate(kRs7GatePegs),
    Rs7_WrapRow(Rs7_Site("PSN2").Gate(kRs7GatePegs), kRs7WrapPSN2, &Rs7_Mid_PSN2Before, &Rs7_Mid_PSN2),
    Rs7_WrapRow(Rs7_Site("PSN3").Gate(kRs7GatePegs), kRs7WrapPSN3, &Rs7_Mid_PSN3Before, nullptr),
};

// The 7-string model and the tuner letters with the refs of the feature (group Core).
const Rs7Hook kCoreHooks[] = {
    Rs7_Mid("LT0", &Rs7_Mid_LT0),
    Rs7_Mid("LT1", &Rs7_Mid_LT1, &g_rs7_resume_LT1),
    Rs7_Mid("PGH1", &Rs7_Mid_PGH1),
    Rs7_Ref("X_EULER", &g_rs7_fn_euler),
    Rs7_Ref("X_PEGNAMES", &g_rs7_pegnames),
    Rs7_Ref("X_PEGSELECT", &g_rs7_fn_pegselect),
    Rs7_Ref("X_PEGPLACE", &g_rs7_fn_pegplace),
    Rs7_Ref("X_PEGLAYER", &g_rs7_fn_peglayer),
    Rs7_Ref("X_FIXSTR", &g_rs7_fn_fixstr),
    Rs7_Mid("AL1", &Rs7_Mid_AL1, &g_rs7_resume_AL1),
    Rs7_Ref("X_STRPARAMS", &g_rs7_fn_strparams),
    Rs7_Ref("X_DDSH", &g_rs7_fn_dds),
    Rs7_Ref("X_NFCTOR", &g_rs7_fn_nfctor),
    Rs7_Ref("X_NFDTOR", &g_rs7_fn_nfdtor),
};
}  // namespace

extern const Rs7HookTable kRs7HeadstockHooks = {Rs7_Group(Rs7Feature::Pegs), kPegHooks,
                                                sizeof kPegHooks / sizeof kPegHooks[0]};
extern const Rs7HookTable kRs7HeadstockCoreHooks = {Rs7_Group(Rs7Feature::Core), kCoreHooks,
                                                    sizeof kCoreHooks / sizeof kCoreHooks[0]};
