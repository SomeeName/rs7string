// B1 - 0x7A2BCA in the chord box builder 0x7A2500, after Rnd_GemGeom_Ctor of the frame (mode 4): EAX = the geometry
// component (or 0), [EBP+8] = chord render object, [EBP+0Ch] = the highway (guitar at +37Ch). Rs7Chord_OnBoxGeom
// marks the component of every chord frame of a 7/8-string arrangement (B2 scales the marked ones); the thunk then
// replays `mov esi,[ebp+18h]; push eax; call 0x7A88B0`. Full mode: runs per chord frame built, and the helper uses
// SSE (struct copies, CRT), which the light thunk does not save.
#include "features/chords/chords.h"
#include "platform/midhook.h"

using Rs7Mid::Dword;

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_B1(Rs7Mid::Context& c) {
    Rs7Chord_OnBoxGeom(reinterpret_cast<void*>(c.eax), reinterpret_cast<const void*>(Dword(c.ebp + 8)),
                       reinterpret_cast<const void*>(Dword(c.ebp + 0x0C)));
}
