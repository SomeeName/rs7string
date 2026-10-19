// The fret bars' hooks: looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Group Core, as
// before the move: F1 and IN1 are base 7-string sites (no switch of their own; [neck] fret_extend is read at run
// time), X_SETSCALE is a data reference (Entity_SetScale, also used by the headstock).
#include "features/frets/frets.h"
#include "features/frets/hooks.h"
#include "model/groups.h"

namespace {
const Rs7Hook kHooks[] = {
    {"X_SETSCALE", nullptr, nullptr, &g_rs7_fn_setscale, false, true},
    Rs7_WrapRow({"F1", nullptr, nullptr, nullptr, false, false}, kRs7WrapF1, nullptr, &Rs7_Mid_F1),
    Rs7_WrapRow({"IN1", nullptr, nullptr, nullptr, false, false}, kRs7WrapIN1, nullptr, &Rs7_Mid_IN1),
};
}  // namespace

extern const Rs7HookTable kRs7FretHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
