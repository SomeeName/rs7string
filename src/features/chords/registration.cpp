// The chord loops' hooks (group Chords): looked up by src/registry.cpp (Rs7_FindHook)
// and written by the main batch, all together and only with native_strings (Rs7_ShouldPatch).
// The six jmp sites (CLBM CLBA CLGU CLGE CLVB CLVE) are light mid-hooks (hooks_cl.cpp); the others have a null stub,
// they are in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/chords/chords.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" void Rs7_Mid_B1(Rs7Mid::Context&);  // hooks_frames.cpp
extern "C" void Rs7_Stub_B2();                 // hooks_stubs.cpp
extern "C" uintptr_t g_rs7_target_B2, g_rs7_fn_sst;

extern "C" {
#define CL_DECL(id)                                                                                                    \
    void Rs7_Mid_##id(Rs7Mid::Context&);                                                                               \
    extern uintptr_t g_rs7_resume_##id;
CL_DECL(CLBM) CL_DECL(CLBA) CL_DECL(CLGU) CL_DECL(CLGE) CL_DECL(CLVB) CL_DECL(CLVE)
#undef CL_DECL
}

namespace {
#define CL_MID(id) Rs7_Mid(#id, &Rs7_Mid_##id, &g_rs7_resume_##id).Gate(kRs7GateChordloops).Light()
const Rs7Hook kHooks[] = {
    Rs7_Site("CLB0").Gate(kRs7GateChordloops),
    CL_MID(CLBM),
    Rs7_Site("CLBF").Gate(kRs7GateChordloops),
    Rs7_Site("CLB1").Gate(kRs7GateChordloops),
    Rs7_Site("CLBG").Gate(kRs7GateChordloops),
    CL_MID(CLBA),
    CL_MID(CLGU),
    CL_MID(CLGE),
    CL_MID(CLVB),
    CL_MID(CLVE),
    Rs7_Site("CLNF2").Gate(kRs7GateChordloops),
    Rs7_Site("CLNF3").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT4").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT5").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT6").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT7").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT8").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT9").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT10").Gate(kRs7GateChordloops),
    Rs7_Site("CLCT11").Gate(kRs7GateChordloops),
};
#undef CL_MID

const Rs7Hook kFrameHooks[] = {
    Rs7_Mid("B1", &Rs7_Mid_B1),
    Rs7_Ref("X_SST", &g_rs7_fn_sst),
    Rs7_Stub("B2", reinterpret_cast<void*>(&Rs7_Stub_B2), nullptr, &g_rs7_target_B2),
};
}  // namespace

extern const Rs7HookTable kRs7ChordHooks = {Rs7_Group(Rs7Feature::Chords), kHooks, sizeof kHooks / sizeof kHooks[0]};
extern const Rs7HookTable kRs7ChordFrameHooks = {Rs7_Group(Rs7Feature::Core), kFrameHooks,
                                                 sizeof kFrameHooks / sizeof kFrameHooks[0]};
