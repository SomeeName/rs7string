// The chord-diagram hooks ([chords] diagrams7): looked up by src/registry.cpp (Rs7_FindHook) and written by the
// main batch, DGB DGM DGS TP13 (full-mode mid-hooks, hooks_mid.cpp) only with g_rs7_diagrams (Rs7_ShouldPatch). Group
// Diagrams; the X_D* refs (the game's dictionary functions GamePut calls) are data references and stay in the Core
// group (Rs7_FeatureOf).
#include "features/diagrams/diagrams.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_DGM(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_DGS(Rs7Mid::Context&);
void Rs7_Mid_TP13(Rs7Mid::Context&);
void Rs7_Mid_DGB(Rs7Mid::Context&);
extern uintptr_t g_rs7_resume_TP13;
extern uintptr_t g_rs7_dkey, g_rs7_dnumber, g_rs7_dset, g_rs7_ddrop;
}

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("DGM", &Rs7_Mid_DGM).Gate(kRs7GateDiagrams),
    Rs7_Mid("DGB", &Rs7_Mid_DGB).Gate(kRs7GateDiagrams),
    Rs7_Mid("DGS", &Rs7_Mid_DGS).Gate(kRs7GateDiagrams),
    Rs7_Mid("TP13", &Rs7_Mid_TP13, &g_rs7_resume_TP13).Gate(kRs7GateDiagrams),
    Rs7_Ref("X_DKEY", &g_rs7_dkey),
    Rs7_Ref("X_DNUMBER", &g_rs7_dnumber),
    Rs7_Ref("X_DSET", &g_rs7_dset),
    Rs7_Ref("X_DDROP", &g_rs7_ddrop),
};
}  // namespace

extern const Rs7HookTable kRs7DiagramHooks = {Rs7_Group(Rs7Feature::Diagrams), kHooks,
                                              sizeof kHooks / sizeof kHooks[0]};
