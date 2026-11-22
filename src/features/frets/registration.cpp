// The fret bars' hooks: looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch. Group Core, as
// before the move: F1 and IN1 are base 7-string sites (no switch of their own; [neck] fret_extend is read at run
// time), X_SETSCALE is a data reference (Entity_SetScale, also used by the headstock).
#include "features/frets/frets.h"
#include "features/frets/hooks.h"
#include "model/groups.h"

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Ref("X_SETSCALE", &g_rsxr_fn_setscale),
    Rsxr_WrapRow(Rsxr_Site("F1"), kRsxrWrapF1, nullptr, &Rsxr_Mid_F1),
    Rsxr_WrapRow(Rsxr_Site("IN1"), kRsxrWrapIN1, nullptr, &Rsxr_Mid_IN1),
    Rsxr_Mid("SC1", &Rsxr_Mid_SC1),
    Rsxr_Mid("FRG1", &Rsxr_Mid_FRG1),
    Rsxr_Mid("FRG2", &Rsxr_Mid_FRG2),
    Rsxr_Mid("FRG2B", &Rsxr_Mid_FRG2),
    Rsxr_Mid("FRG3", &Rsxr_Mid_FRG3),
};
}  // namespace

extern const RsxrHookTable kRsxrFretHooks = {Rsxr_Group(RsxrFeature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
