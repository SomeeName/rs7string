// The song-end log's hooks, looked up by src/registry.cpp (Rs7_FindHook) and written by the main batch. Group Core,
// as before the move.
#include "features/songend/songend.h"
#include "model/groups.h"

extern "C" {
void Rs7_Mid_SE1(Rs7Mid::Context&);  // hooks.cpp
void Rs7_Mid_SE2(Rs7Mid::Context&);
void Rs7_Mid_SE3(Rs7Mid::Context&);
}

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("SE1", &Rs7_Mid_SE1),
    Rs7_Mid("SE2", &Rs7_Mid_SE2),
    Rs7_Mid("SE3", &Rs7_Mid_SE3),
};
}  // namespace

extern const Rs7HookTable kRs7SongEndHooks = {Rs7_Group(Rs7Feature::Core), kHooks, sizeof kHooks / sizeof kHooks[0]};
