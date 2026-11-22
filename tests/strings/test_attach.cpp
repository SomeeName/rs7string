#include "features/spacing/spacing.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "platform/seh.h"
#include "test.h"
#include <windows.h>

namespace {
unsigned g_calls = 0, g_faultAt = 0;

uint32_t __cdecl Attach(uint32_t s, float* out) {
    if (++g_calls == g_faultAt) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }

    out[0] = out[1] = 0.0f;
    out[2] = float(s);

    return 1;
}

bool AttachFaults(uint8_t* info, float* out) {
    __try {
        RsxrGs_AttachSpaced(info, 0, out);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return true;
    }

    return false;
}
}  // namespace

TEST(strings_spacing_recovers_after_an_attach_fault) {
    const uintptr_t savedAttach = g_rsxr_fn_attach;
    const float savedSpacing = g_rsxr_spacing[2];
    uint8_t info[rsxrgs::kInfoSize] = {1};
    float out[3] = {};

    info[rsxr::kInfoStrings] = 7;
    g_rsxr_fn_attach = uintptr_t(&Attach);
    g_rsxr_spacing[2] = 2.0f;

    for (unsigned faultAt : {1u, 2u, 4u}) {
        g_calls = 0;
        g_faultAt = faultAt;
        CHECK(AttachFaults(info, out));
        g_calls = 0;
        g_faultAt = 0;
        CHECK_EQ(RsxrGs_AttachSpaced(info, 0, out), 1);
        CHECK_EQ(g_calls, 4u);
        CHECK_EQ(out[2], -2.5f);
    }

    RsxrSpacing_Forget(info);
    g_rsxr_fn_attach = savedAttach;
    g_rsxr_spacing[2] = savedSpacing;
}
