#include "model/eight.h"
#include "platform/config.h"
#include "platform/settings.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_eight = 0;

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7EightConfig g_config;

void Read(const wchar_t* iniPath) {
    g_config = Rs7Eight_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"neck.eight", L"neck", L"eight", T::Bool, W::Restart, "0", 0, 1, "8-string preparation",
     "Experimental: chord columns 6/7 and virtual-guitar string 7. 8Str names follow [tuner] names; native "
     "publish/tuner and the eighth peg are separate work.",
     nullptr},
};
}  // namespace

const Rs7SetTable kRs7EightSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

const Rs7EightConfig& Rs7Eight_Config() {
    return g_config;
}

Rs7EightConfig Rs7Eight_ReadIni(const wchar_t* iniPath) {
    Rs7EightConfig c;
    if (!iniPath) {
        return c;
    }

    c.eightRaw = Rs7Config_ReadInt(L"neck", L"eight", 0, iniPath);
    c.eight = c.eightRaw != 0;

    return c;
}
