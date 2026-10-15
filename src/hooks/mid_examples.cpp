#include "core/diag.h"
#include "hooks/midhook.h"

// CONFIRMED: D2 @0x7A22E0 and D3 @0x7A90F5 (September 2022),
// identical arguments/filtering to the former naked stubs; stock bytes replay
// through the generic relocator. Enabled only by [debug] diagnostics=1.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_D2(Rs7Mid::Context& c) {
    const auto* stack = reinterpret_cast<const uint32_t*>(c.esp);
    const uint8_t string = uint8_t(stack[4]);

    if (string == 0 || string == 6) {
        Rs7_DiagGemCreate(stack[0], c.eax, stack[1], stack[3], stack[4], stack[6]);
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_D3(Rs7Mid::Context& c) {
    Rs7_DiagGeomBuild(reinterpret_cast<const uint8_t*>(c.edi));
}
