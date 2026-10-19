// The chord-diagram hooks ([chords] diagrams7): looked up by src/registry.cpp (Rs7_FindHook) and written by the
// main batch, DGB DGM DGS TP13 (full-mode mid-hooks, hooks_mid.cpp) only with g_rs7_diagrams (Rs7_ShouldPatch). Group
// Diagrams; the X_D* refs (the game's dictionary functions GamePut calls) are data references and stay in the Core
// group (Rs7_FeatureOf).
#include "features/diagrams/diagrams.h"
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
    {"DGM", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_DGM},
    {"DGB", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_DGB},
    {"DGS", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_DGS},
    {"TP13", nullptr, &g_rs7_resume_TP13, nullptr, false, false, false, 0, 0, &Rs7_Mid_TP13},
    {"X_DKEY", nullptr, nullptr, &g_rs7_dkey, false, true},
    {"X_DNUMBER", nullptr, nullptr, &g_rs7_dnumber, false, true},
    {"X_DSET", nullptr, nullptr, &g_rs7_dset, false, true},
    {"X_DDROP", nullptr, nullptr, &g_rs7_ddrop, false, true},
};
}  // namespace

extern const Rs7HookTable kRs7DiagramHooks = {Rs7_Group(Rs7Feature::Diagrams), kHooks,
                                              sizeof kHooks / sizeof kHooks[0]};
