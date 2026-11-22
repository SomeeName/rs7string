// The chord-diagram hooks ([chords] diagrams7): looked up by src/registry.cpp (Rsxr_FindHook) and written by the
// main batch, DGB DGM DGS TP13 (full-mode mid-hooks, hooks_mid.cpp) only with g_rsxr_diagrams (Rsxr_ShouldPatch). Group
// Diagrams; the X_D* refs (the game's dictionary functions GamePut calls) are data references and stay in the Core
// group (Rsxr_FeatureOf).
#include "features/diagrams/diagrams.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_DGM(RsxrMid::Context&);  // hooks_mid.cpp
void Rsxr_Mid_DGS(RsxrMid::Context&);
void Rsxr_Mid_TP13(RsxrMid::Context&);
void Rsxr_Mid_DGB(RsxrMid::Context&);
extern uintptr_t g_rsxr_resume_TP13;
extern uintptr_t g_rsxr_dkey, g_rsxr_dnumber, g_rsxr_dset, g_rsxr_ddrop;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("DGM", &Rsxr_Mid_DGM).Gate(kRsxrGateDiagrams),
    Rsxr_Mid("DGB", &Rsxr_Mid_DGB).Gate(kRsxrGateDiagrams),
    Rsxr_Mid("DGS", &Rsxr_Mid_DGS).Gate(kRsxrGateDiagrams),
    Rsxr_Mid("TP13", &Rsxr_Mid_TP13, &g_rsxr_resume_TP13).Gate(kRsxrGateDiagrams),
    Rsxr_Ref("X_DKEY", &g_rsxr_dkey),
    Rsxr_Ref("X_DNUMBER", &g_rsxr_dnumber),
    Rsxr_Ref("X_DSET", &g_rsxr_dset),
    Rsxr_Ref("X_DDROP", &g_rsxr_ddrop),
};
}  // namespace

extern const RsxrHookTable kRsxrDiagramHooks = {Rsxr_Group(RsxrFeature::Diagrams), kHooks,
                                                sizeof kHooks / sizeof kHooks[0]};
