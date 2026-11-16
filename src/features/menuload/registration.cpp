#include "features/menuload/menuload.h"
#include "model/gates.h"
#include "model/groups.h"

extern "C" void Rs7_Mid_MenuMovieSetup(Rs7Mid::Context&);
extern "C" void Rs7_Mid_MenuMovieResult(Rs7Mid::Context&);
extern "C" void Rs7_Mid_MenuIndexPublish(Rs7Mid::Context&);
extern "C" void Rs7_Mid_MenuLoadUpdate(Rs7Mid::Context&);
extern "C" __declspec(dllexport) uintptr_t g_rs7_menu_request;

namespace {
const Rs7Hook kHooks[] = {
    Rs7_Mid("MenuMovieSetup", &Rs7_Mid_MenuMovieSetup).Gate(kRs7GateMenuLoad),
    Rs7_Mid("MenuMovieResult", &Rs7_Mid_MenuMovieResult).Gate(kRs7GateMenuLoad),
    Rs7_Mid("MenuIndexPublish", &Rs7_Mid_MenuIndexPublish).Gate(kRs7GateMenuLoad),
    Rs7_Mid("MenuLoadUpdate", &Rs7_Mid_MenuLoadUpdate).Gate(kRs7GateMenuLoad),
    Rs7_Ref("MenuMovieRequest", &g_rs7_menu_request).Gate(kRs7GateMenuLoad),
};
}

extern const Rs7HookTable kRs7MenuLoadHooks = {Rs7_Group(Rs7Feature::MenuLoad), kHooks,
                                               sizeof kHooks / sizeof kHooks[0]};
