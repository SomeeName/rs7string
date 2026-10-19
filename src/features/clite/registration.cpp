// C-lite's hooks (group Clite), looked up by src/registry.cpp (Rs7_FindHook): the string count as a property of the
// guitar build, written only with g_rs7_clite (Rs7_ShouldPatch).
#include "features/clite/clite.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_GKC(Rs7Mid::Context&);  // clite.cpp
void Rs7_Mid_GKE(Rs7Mid::Context&);
void Rs7_Mid_GKM(Rs7Mid::Context&);
void Rs7_Mid_GKL(Rs7Mid::Context&);
void Rs7_Mid_GKT(Rs7Mid::Context&);
void Rs7_Mid_GKH1(Rs7Mid::Context&);
void Rs7_Mid_GKH2(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    {"GKC", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKC},
    {"GKE", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKE},
    {"GKM", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKM},
    {"GKL", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKL},
    {"GKT", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKT},
    {"GKH1", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKH1},
    {"GKH2", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7_Mid_GKH2},
};
}  // namespace

extern const Rs7HookTable kRs7CliteHooks = {Rs7_Group(Rs7Feature::Clite), kHooks, sizeof kHooks / sizeof kHooks[0]};
