#include "features/headstock/hooks.h"
#include "features/headstock/assetload.h"
#include "features/headstock/state.h"
#include "platform/log.h"

// The headstock's switches, refs and state, and its startup configuration.

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_head_on = 1;
__declspec(dllexport) uintptr_t g_rsxr_native_pegset = 0;
__declspec(dllexport) uintptr_t g_rsxr_peg_load = 7;
__declspec(dllexport) uintptr_t g_rsxr_fn_euler = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_pegplace = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_peglayer = 0;
__declspec(dllexport) uintptr_t g_rsxr_pegnames = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_pegselect = 0;
}

rsxrhead::State rsxrhead::g_head{};

// The log lines of the PegSet glue (exported: the emulator tests replace it with `ret`).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rsxr_HeadLog(const char* fmt, const void* a,
                                                                                const void* b, const void* c) {
    try {
        RsxrLog(fmt, a, b, c);
    } catch (...) {
    }
}

void RsxrHeadstock_Configure(const wchar_t* iniPath) {
    g_rsxr_native_pegset = 1;  // always on
    RsxrHead_SetIni(iniPath);
    RsxrAsset_Configure(iniPath);
}
