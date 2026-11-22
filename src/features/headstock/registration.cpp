#include "features/headstock/assetload.h"
#include "features/headstock/headstock.h"
#include "features/headstock/hooks.h"
#include "model/gates.h"
#include "model/groups.h"

// The headstock's hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch.

namespace {
// A native-PegSet row: written only with the native PegSet (Rsxr_ShouldPatch; PSQ1 not with clite, GKC replaces it).

// The native 8-slot PegSet (group Pegs): allocation, ctor, load, unload, destructor, free, mesh collection, colours.
const RsxrHook kPegHooks[] = {
    Rsxr_Site("PSA1").Gate(kRsxrGatePegs),
    Rsxr_Mid("PSA2", &Rsxr_Mid_PSA2, &g_rsxr_resume_PSA2).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSD1", &Rsxr_Mid_PSD1, &g_rsxr_resume_PSD1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSD2", &Rsxr_Mid_PSD2, &g_rsxr_resume_PSD2).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSU1", &Rsxr_Mid_PSU1, &g_rsxr_resume_PSU1, &g_rsxr_target_PSU1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSU2", &Rsxr_Mid_PSU2, &g_rsxr_resume_PSU2, &g_rsxr_target_PSU2).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSU3", &Rsxr_Mid_PSU3, &g_rsxr_resume_PSU3).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSM1", &Rsxr_Mid_PSM1, &g_rsxr_resume_PSM1, &g_rsxr_target_PSM1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSM2", &Rsxr_Mid_PSM2, &g_rsxr_resume_PSM2, &g_rsxr_target_PSM2).Gate(kRsxrGatePegs),
    Rsxr_Site("PSQ1").Gate(kRsxrGateNoClite).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSC1", &Rsxr_Mid_PSC1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSL1", &Rsxr_Mid_PSL1, &g_rsxr_resume_PSL1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSL2", &Rsxr_Mid_PSL2, nullptr, &g_rsxr_target_PSL2).Gate(kRsxrGatePegs).After(),
    Rsxr_Mid("PSR1", &Rsxr_Mid_PSR1, &g_rsxr_resume_PSR1).Gate(kRsxrGatePegs),
    Rsxr_Mid("PSN1", &Rsxr_Mid_PSN1, &g_rsxr_resume_PSN1).Gate(kRsxrGatePegs),
    Rsxr_WrapRow(Rsxr_Site("PSN2").Gate(kRsxrGatePegs), kRsxrWrapPSN2, &Rsxr_Mid_PSN2Before, &Rsxr_Mid_PSN2),
    Rsxr_WrapRow(Rsxr_Site("PSN3").Gate(kRsxrGatePegs), kRsxrWrapPSN3, &Rsxr_Mid_PSN3Before, nullptr),
};

// The 7-string model and the tuner letters with the refs of the feature (group Core).
const RsxrHook kCoreHooks[] = {
    Rsxr_Mid("LT0", &Rsxr_Mid_LT0),
    Rsxr_Mid("LT1", &Rsxr_Mid_LT1, &g_rsxr_resume_LT1),
    Rsxr_Mid("PGH1", &Rsxr_Mid_PGH1),
    Rsxr_Ref("X_EULER", &g_rsxr_fn_euler),
    Rsxr_Ref("X_PEGNAMES", &g_rsxr_pegnames),
    Rsxr_Ref("X_PEGSELECT", &g_rsxr_fn_pegselect),
    Rsxr_Ref("X_PEGPLACE", &g_rsxr_fn_pegplace),
    Rsxr_Ref("X_PEGLAYER", &g_rsxr_fn_peglayer),
    Rsxr_Ref("X_FIXSTR", &g_rsxr_fn_fixstr),
    Rsxr_Mid("AL1", &Rsxr_Mid_AL1, &g_rsxr_resume_AL1),
    Rsxr_Ref("X_STRPARAMS", &g_rsxr_fn_strparams),
    Rsxr_Ref("X_DDSH", &g_rsxr_fn_dds),
    Rsxr_Ref("X_NFCTOR", &g_rsxr_fn_nfctor),
    Rsxr_Ref("X_NFDTOR", &g_rsxr_fn_nfdtor),
};
}  // namespace

extern const RsxrHookTable kRsxrHeadstockHooks = {Rsxr_Group(RsxrFeature::Pegs), kPegHooks,
                                                  sizeof kPegHooks / sizeof kPegHooks[0]};
extern const RsxrHookTable kRsxrHeadstockCoreHooks = {Rsxr_Group(RsxrFeature::Core), kCoreHooks,
                                                      sizeof kCoreHooks / sizeof kCoreHooks[0]};
