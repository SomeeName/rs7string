#include "features/nextgrid/nextgrid.h"

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_native_nextgrid = 0;
}

namespace rsxrng {
void Configure(bool native) {
    g_rsxr_native_nextgrid = native ? 1 : 0;
}
}  // namespace rsxrng
