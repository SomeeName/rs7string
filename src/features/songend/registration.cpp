// The song-end log's hooks, looked up by src/registry.cpp (Rsxr_FindHook) and written by the main batch. Group Core,
// as before the move.
#include "features/songend/songend.h"
#include "model/groups.h"

extern "C" {
void Rsxr_Mid_SE1(RsxrMid::Context&);  // hooks.cpp
void Rsxr_Mid_SE2(RsxrMid::Context&);
void Rsxr_Mid_SE3(RsxrMid::Context&);
}

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("SE1", &Rsxr_Mid_SE1),
    Rsxr_Mid("SE2", &Rsxr_Mid_SE2),
    Rsxr_Mid("SE3", &Rsxr_Mid_SE3),
};
}  // namespace

extern const RsxrHookTable kRsxrSongEndHooks = {Rsxr_Group(RsxrFeature::Core), kHooks,
                                                sizeof kHooks / sizeof kHooks[0]};
