// The virtual guitarist's hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch,
// TICK / JUD1 / JUD2 / AUD1 only with [test] vguitar=1 (vgOnly, Rs7_ShouldPatch). X_VGSLOT / X_VGDI are ref sites.
#include "features/vguit/vguit.h"
#include "model/groups.h"
#include <cstdint>

extern "C" {
void Rs7_Mid_TICK(Rs7Mid::Context&);  // hooks_mid.cpp
void Rs7_Mid_JUD1(Rs7Mid::Context&);
void Rs7_Mid_JUD2(Rs7Mid::Context&);
void Rs7_Mid_AUD1(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    {"X_VGSLOT", nullptr, nullptr, &g_rs7_vg_slotmap, false, true},
    {"X_VGDI", nullptr, nullptr, &g_rs7_vg_dimgr, false, true},
    {"TICK", nullptr, nullptr, nullptr, false, false, true, 0, 0, &Rs7_Mid_TICK},
    {"JUD1", nullptr, nullptr, nullptr, false, false, true, 0, 0, &Rs7_Mid_JUD1},
    {"JUD2", nullptr, nullptr, nullptr, false, false, true, 0, 0, &Rs7_Mid_JUD2},
    {"AUD1", nullptr, nullptr, nullptr, false, false, true, 0, 0, &Rs7_Mid_AUD1},
};
}  // namespace

extern const Rs7HookTable kRs7VguitHooks = {Rs7_Group(Rs7Feature::Vguitar), kHooks, sizeof kHooks / sizeof kHooks[0]};
