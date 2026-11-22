#include "features/menuload/menuload.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" void Rsxr_Mid_MenuMovieSetup(RsxrMid::Context&);
extern "C" void Rsxr_Mid_MenuMovieResult(RsxrMid::Context&);
extern "C" void Rsxr_Mid_MenuIndexPublish(RsxrMid::Context&);
extern "C" void Rsxr_Mid_MenuLoadUpdate(RsxrMid::Context&);
extern "C" __declspec(dllexport) uintptr_t g_rsxr_menu_request;

namespace {
const RsxrHook kHooks[] = {
    Rsxr_Mid("MenuMovieSetup", &Rsxr_Mid_MenuMovieSetup).Gate(kRsxrGateMenuLoad),
    Rsxr_Mid("MenuMovieResult", &Rsxr_Mid_MenuMovieResult).Gate(kRsxrGateMenuLoad),
    Rsxr_Mid("MenuIndexPublish", &Rsxr_Mid_MenuIndexPublish).Gate(kRsxrGateMenuLoad),
    Rsxr_Mid("MenuLoadUpdate", &Rsxr_Mid_MenuLoadUpdate).Gate(kRsxrGateMenuLoad),
    Rsxr_Ref("MenuMovieRequest", &g_rsxr_menu_request).Gate(kRsxrGateMenuLoad),
};
}

extern const RsxrHookTable kRsxrMenuLoadHooks = {Rsxr_Group(RsxrFeature::MenuLoad), kHooks,
                                                 sizeof kHooks / sizeof kHooks[0]};
