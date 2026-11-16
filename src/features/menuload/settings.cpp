#include "features/menuload/menuload.h"

uintptr_t g_rs7_menu_retry = 1;

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;
const Rs7SetDef kDefs[] = {
    {"fixes.menu_retry", L"fixes", L"menu_retry", T::Bool, W::Restart, "1", 0, 1, "Menu loading retry",
     "Retries a missing menu movie during loading. 0: off. Applies after a restart.", nullptr},
};

void Read(const wchar_t* iniPath) {
    g_rs7_menu_retry = uintptr_t(Rs7Set_ReadTableInt(kRs7MenuLoadSettings, "fixes.menu_retry", iniPath));
}
}  // namespace

const Rs7SetTable kRs7MenuLoadSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), &Read};
