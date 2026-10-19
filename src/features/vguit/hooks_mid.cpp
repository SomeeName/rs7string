// The virtual guitarist's site hooks ([test] vguitar=1 only: Rs7_Startup writes these four sites only then;
// docs/ARCHITECTURE.md section 12). Each calls its cdecl C entry (hooks.cpp), then the thunk replays the overwritten
// instructions. g_rs7_vg_on = 0 (a fault switched the mode off) skips the call.
//   TICK  entry of Play_Session_UpdateNoteDetection 0x7E2880: Rs7Vg_OnTick(session [esp+4], song time [esp+8])
//   JUD1  0x7E2CFF in 0x7E2880 (EBX = note object, ESI = session): Rs7Vg_OnJudge(note, session)
//   JUD2  entry of Play_Session_ScoreNote 0x7E6AD0 (CL = hit, EDX = session, [esp+4] = note): Rs7Vg_OnScore
//   AUD1  0xC4618C in SndIn_PA_Read 0xC460C0 (ESI = device, EBX = AkAudioBuffer; Wwise audio thread), after the FIFO
//         copy, before the detection callback dev+4: Rs7Vg_OnInput(dev, buf) writes the synthesized samples
// Full-mode mid-hooks (once per frame, per judged note, per audio buffer; the C entries use floats), converted from
// naked stubs that saved the whole state (refactor step 7 batch 5, frozen at 9c0e44f,
// tools/tests/test_midhook_features.py). The replays `push ebp; mov ebp,esp; and esp,-40h` (TICK, JUD2) and `mov
// eax,[esi+4]; test eax,eax` (AUD1) write the flags the game reads; JUD1's `mov byte [ebx+0C3h],1` writes none, and the
// old stub's off path left its switch test's flags there (the next instruction is a cmp: contract "dead").
#include "features/vguit/hooks.h"
#include "platform/midhook.h"
#include <cstdint>
#include <cstring>

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TICK(Context& c) {
    if (!g_rs7_vg_on) {
        return;
    }

    const uint32_t bits = Dword(c.esp + 8);
    float songTime;

    std::memcpy(&songTime, &bits, sizeof songTime);
    Rs7Vg_OnTick(Rs7Mid::Ptr<void>(Dword(c.esp + 4)), songTime);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_JUD1(Context& c) {
    if (g_rs7_vg_on) {
        Rs7Vg_OnJudge(Rs7Mid::Ptr<void>(c.ebx), Rs7Mid::Ptr<void>(c.esi));
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_JUD2(Context& c) {
    if (g_rs7_vg_on) {
        Rs7Vg_OnScore(static_cast<int>(c.ecx & 0xFF), Rs7Mid::Ptr<void>(c.edx), Rs7Mid::Ptr<void>(Dword(c.esp + 4)));
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AUD1(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (g_rs7_vg_on) {
        Rs7Vg_OnInput(Rs7Mid::Ptr<void>(c.esi), Rs7Mid::Ptr<void>(c.ebx));
    }
}
