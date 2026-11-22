// FGRB - local points of the extra FretGrid cells 6/7: 0x58FA25 `mov eax, 2` in Rnd_FretGrid_Build 0x58F8A0 (the
// "complete" exit; EBP frame: [ebp+8] = FretGrid, [ebp+0Ch] = guitar; x87 empty). Builds the local points of cells 6/7,
// then the replay runs. Full-mode mid-hook (once per grid build); flags contract "dead" (the incoming flags are left,
// tools/tests/test_midhook_features.py).
#include "features/fretgrid/fretgrid.h"
#include "platform/midhook.h"

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_FGRB(RsxrMid::Context& c) {
    RsxrFg_BuildExtra(reinterpret_cast<uint8_t*>(RsxrMid::Dword(c.ebp + 8)),
                      reinterpret_cast<uint8_t*>(RsxrMid::Dword(c.ebp + 0x0C)));
}
