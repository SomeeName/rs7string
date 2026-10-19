#include "features/headstock/hooks.h"
#include "features/headstock/assetload.h"
#include "features/headstock/state.h"
#include "platform/log.h"

// The headstock's switches, refs and state, and its startup configuration.

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_head_on = 1;
__declspec(dllexport) uintptr_t g_rs7_native_pegset = 0;
__declspec(dllexport) uintptr_t g_rs7_peg_load = 7;
__declspec(dllexport) uintptr_t g_rs7_fn_euler = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_pegplace = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_peglayer = 0;
__declspec(dllexport) uintptr_t g_rs7_pegnames = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_pegselect = 0;
}

rs7head::State rs7head::g_head{};

// The log lines of the PegSet glue (exported: the emulator tests replace it with `ret`).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rs7_HeadLog(const char* fmt, const void* a,
                                                                               const void* b, const void* c) {
    try {
        Rs7Log(fmt, a, b, c);
    } catch (...) {
    }
}

void Rs7Headstock_Configure(const wchar_t* iniPath) {
    const Rs7HeadstockConfig& c = Rs7Headstock_Config();

    g_rs7_head_on = c.headstockPeg ? 1 : 0;
    g_rs7_native_pegset = 1;  // mandatory since 1.0
    Rs7Head_SetIni(iniPath);
    Rs7Log("config: [neck] headstock_peg=%d -> 7th tuning peg %s", c.headstockPegRaw,
           !c.headstockPeg
               ? "off"
               : "native 7-slot PegSet: 6-string guitars load 7 pegs, peg 7 active on 7-string songs / tuners");
    Rs7Asset_Configure(iniPath);
}
