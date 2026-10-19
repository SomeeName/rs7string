#include "features/nextgrid/nextgrid.h"

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_native_nextgrid = 0;
}

namespace rs7ng {
void Configure(bool native) {
    g_rs7_native_nextgrid = native ? 1 : 0;
}
}  // namespace rs7ng
