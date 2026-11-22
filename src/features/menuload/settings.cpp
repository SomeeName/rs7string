#include "features/menuload/menuload.h"

uintptr_t g_rsxr_menu_retry = 1;

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;
const RsxrSetDef kDefs[] = {
    {"fixes.menu_retry", L"fixes", L"menu_retry", T::Bool, W::Restart, "1", 0, 1, "Menu loading retry",
     "Retries a missing menu movie during loading. 0: off. Applies after a restart.", nullptr},
};

void Read(const wchar_t* iniPath) {
    g_rsxr_menu_retry = uintptr_t(RsxrSet_ReadTableInt(kRsxrMenuLoadSettings, "fixes.menu_retry", iniPath));
}
}  // namespace

const RsxrSetTable kRsxrMenuLoadSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), &Read};
