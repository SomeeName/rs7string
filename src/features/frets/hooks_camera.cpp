// SC1 - 0x593A1A in Rnd_Guitar_SendCameraMarkers (0x593980), before the guitar in ESI becomes InstrumentInfo.
// The outgoing vector z values are [ESP+18h], [ESP+24h], [ESP+30h] and EDX (float bits from guitar+34h, NOT a
// pointer). The fourth is stored at [ESP+3Ch] after replay. Shift all four with the floor, not the persistent
// computed pair or the guitar position: each send starts from fresh stock values. Full mode, per camera send.
#include "features/frets/hooks.h"
#include "platform/safecopy.h"
#include <cstring>
#include <initializer_list>

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_SC1(RsxrMid::Context& c) {
    if (!g_rsxr_frets_on) {
        return;
    }

    uint8_t* guitar = RsxrMid::Ptr(c.esi);
    uint8_t* grid = nullptr;
    float shift[3];
    if (!guitar || !Rsxr_SafeCopyZero(&grid, guitar + rsxr::kGuitarGrid, sizeof grid) ||
        rsxrfretglue::FloorOffset(guitar, grid, shift) <= 0 || shift[2] == 0.0f) {
        return;
    }

    for (const uint32_t offset : {0x18u, 0x24u, 0x30u}) {
        *RsxrMid::Ptr<float>(c.esp + offset) += shift[2];
    }

    float z;

    std::memcpy(&z, &c.edx, sizeof z);
    z += shift[2];
    std::memcpy(&c.edx, &z, sizeof z);
    // Default jump: replay mov esi,[esi+24h]; mov bl,[esi+9] with registers and flags preserved by the thunk.
}
