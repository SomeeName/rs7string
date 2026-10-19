// SF1 - drop the T8 / C8 arrays when the song is freed. A call site (Before placement, ARCHITECTURE.md section 15
// "Call sites"): 0x558686 in the SNG_Song scalar deleting destructor 0x558680 (ESI = this) calls the destructor body
// 0x5586C0. The callback drops the song's arrays and leaves ctx.jump at the callee, which then runs with the game's
// stack and return address. Full mode: Rs7Ns_OnSongFree logs, and the destructor also runs on loader threads.
// Converted from a naked stub (frozen at 44135a2, tools/tests/test_midhook_nslot.py).
#include "features/nslot/nslot.h"
#include "platform/midhook.h"
#include <cstdint>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_target_SF1 = 0;

__declspec(dllexport) void __cdecl Rs7_Mid_SF1(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Ns_OnSongFree(reinterpret_cast<void*>(c.esi));
}
}
