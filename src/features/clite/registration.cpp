// C-lite's hooks (group Clite), looked up by src/registry.cpp (Rsxr_FindHook): the string count as a property of the
// guitar build, written only with g_rsxr_clite (Rsxr_ShouldPatch).
#include "features/clite/clite.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_GKC(RsxrMid::Context&);  // hooks.cpp
void Rsxr_Mid_GKB(RsxrMid::Context&);
void Rsxr_Mid_GKE(RsxrMid::Context&);
void Rsxr_Mid_GKM(RsxrMid::Context&);
void Rsxr_Mid_GKL(RsxrMid::Context&);
void Rsxr_Mid_GKT(RsxrMid::Context&);
void Rsxr_Mid_GKH1(RsxrMid::Context&);
void Rsxr_Mid_GKH2(RsxrMid::Context&);
extern uintptr_t g_rsxr_tuner_bassbase;
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("GKC", &Rsxr_Mid_GKC).Gate(kRsxrGateClite),   Rsxr_Mid("GKB", &Rsxr_Mid_GKB).Gate(kRsxrGateClite),
    Rsxr_Ref("X_TBASS", &g_rsxr_tuner_bassbase),           Rsxr_Mid("GKE", &Rsxr_Mid_GKE).Gate(kRsxrGateClite),
    Rsxr_Mid("GKM", &Rsxr_Mid_GKM).Gate(kRsxrGateClite),   Rsxr_Mid("GKL", &Rsxr_Mid_GKL).Gate(kRsxrGateClite),
    Rsxr_Mid("GKT", &Rsxr_Mid_GKT).Gate(kRsxrGateClite),   Rsxr_Mid("GKH1", &Rsxr_Mid_GKH1).Gate(kRsxrGateClite),
    Rsxr_Mid("GKH2", &Rsxr_Mid_GKH2).Gate(kRsxrGateClite),
};
}  // namespace

extern const RsxrHookTable kRsxrCliteHooks = {Rsxr_Group(RsxrFeature::Clite), kHooks, sizeof kHooks / sizeof kHooks[0]};
