// The chord loops' hooks (group Chords): looked up by src/registry.cpp (Rsxr_FindHook)
// and written by the main batch, all together and only with native_strings (Rsxr_ShouldPatch).
// The six jmp sites (CLBM CLBA CLGU CLGE CLVB CLVE) are light mid-hooks (hooks_cl.cpp); the others have a null stub,
// they are in-place byte patches (the row only registers the site; the bytes come from sites.json).
#include "features/chords/chords.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" void Rsxr_Mid_B1(RsxrMid::Context&);  // hooks_frames.cpp
extern "C" void Rsxr_Stub_B2();                  // hooks_stubs.cpp
extern "C" uintptr_t g_rsxr_target_B2, g_rsxr_fn_sst;

extern "C" {
#define CL_DECL(id)                                                                                                    \
    void Rsxr_Mid_##id(RsxrMid::Context&);                                                                             \
    extern uintptr_t g_rsxr_resume_##id;
CL_DECL(CLBM) CL_DECL(CLBA) CL_DECL(CLGU) CL_DECL(CLGE) CL_DECL(CLVB) CL_DECL(CLVE)
#undef CL_DECL
}

namespace {
#define CL_MID(id) Rsxr_Mid(#id, &Rsxr_Mid_##id, &g_rsxr_resume_##id).Gate(kRsxrGateChordloops).Light()
const RsxrHook kHooks[] = {
    Rsxr_Site("CLB0").Gate(kRsxrGateChordloops),
    CL_MID(CLBM),
    Rsxr_Site("CLBF").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLB1").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLBG").Gate(kRsxrGateChordloops),
    CL_MID(CLBA),
    CL_MID(CLGU),
    CL_MID(CLGE),
    CL_MID(CLVB),
    CL_MID(CLVE),
    Rsxr_Site("CLNF2").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLNF3").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT4").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT5").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT6").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT7").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT8").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT9").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT10").Gate(kRsxrGateChordloops),
    Rsxr_Site("CLCT11").Gate(kRsxrGateChordloops),
};
#undef CL_MID

const RsxrHook kFrameHooks[] = {
    Rsxr_Mid("B1", &Rsxr_Mid_B1),
    Rsxr_Ref("X_SST", &g_rsxr_fn_sst),
    Rsxr_Stub("B2", reinterpret_cast<void*>(&Rsxr_Stub_B2), nullptr, &g_rsxr_target_B2),
};
}  // namespace

extern const RsxrHookTable kRsxrChordHooks = {Rsxr_Group(RsxrFeature::Chords), kHooks,
                                              sizeof kHooks / sizeof kHooks[0]};
extern const RsxrHookTable kRsxrChordFrameHooks = {Rsxr_Group(RsxrFeature::Core), kFrameHooks,
                                                   sizeof kFrameHooks / sizeof kFrameHooks[0]};
