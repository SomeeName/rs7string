// The fret bars' hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Group Core, as
// before the move: F1 and IN1 are base 7-string sites (no switch of their own; [neck] fret_extend is read at run
// time), X_SETSCALE is a data reference (Entity_SetScale, also used by the headstock).
#include "features/frets/frets.h"
#include "features/frets/hooks.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Ref("X_SETSCALE", &g_rs7_fn_setscale),
    Rs7_WrapRow(Rs7_Site("F1"), kRs7WrapF1, nullptr, &Rs7_Mid_F1),
    Rs7_WrapRow(Rs7_Site("IN1"), kRs7WrapIN1, nullptr, &Rs7_Mid_IN1),
    Rs7_Mid("SC1", &Rs7_Mid_SC1),
    Rs7_Mid("FRG1", &Rs7_Mid_FRG1),
    Rs7_Mid("FRG2", &Rs7_Mid_FRG2),
    Rs7_Mid("FRG2B", &Rs7_Mid_FRG2),
    Rs7_Mid("FRG3", &Rs7_Mid_FRG3),
};
}  // namespace

extern const Rs7HookTable kRs7FretHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
