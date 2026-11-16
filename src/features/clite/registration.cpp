// C-lite's hooks (group Clite), looked up by src/registry.cpp (Rs7_FindHook): the string count as a property of the
// guitar build, written only with g_rs7_clite (Rs7_ShouldPatch).
#include "features/clite/clite.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_GKC(Rs7Mid::Context&);  // hooks.cpp
void Rs7_Mid_GKB(Rs7Mid::Context&);
void Rs7_Mid_GKE(Rs7Mid::Context&);
void Rs7_Mid_GKM(Rs7Mid::Context&);
void Rs7_Mid_GKL(Rs7Mid::Context&);
void Rs7_Mid_GKT(Rs7Mid::Context&);
void Rs7_Mid_GKH1(Rs7Mid::Context&);
void Rs7_Mid_GKH2(Rs7Mid::Context&);
extern uintptr_t g_rs7_tuner_bassbase;
}

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("GKC", &Rs7_Mid_GKC).Gate(kRs7GateClite),   Rs7_Mid("GKB", &Rs7_Mid_GKB).Gate(kRs7GateClite),
    Rs7_Ref("X_TBASS", &g_rs7_tuner_bassbase),          Rs7_Mid("GKE", &Rs7_Mid_GKE).Gate(kRs7GateClite),
    Rs7_Mid("GKM", &Rs7_Mid_GKM).Gate(kRs7GateClite),   Rs7_Mid("GKL", &Rs7_Mid_GKL).Gate(kRs7GateClite),
    Rs7_Mid("GKT", &Rs7_Mid_GKT).Gate(kRs7GateClite),   Rs7_Mid("GKH1", &Rs7_Mid_GKH1).Gate(kRs7GateClite),
    Rs7_Mid("GKH2", &Rs7_Mid_GKH2).Gate(kRs7GateClite),
};
}  // namespace

extern const Rs7HookTable kRs7CliteHooks = {Rs7_Group(Rs7Feature::Clite), kHooks, sizeof kHooks / sizeof kHooks[0]};
