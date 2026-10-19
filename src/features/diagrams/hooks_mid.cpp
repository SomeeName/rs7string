// Chord-diagram data mid-hooks ([chords] diagrams7, diagrams.h, docs/ARCHITECTURE.md section 9): the stock diagram
// serializers run, then C++ adds the LowB / LowFs data keys for the 7th / 8th string row. diagrams7 gates every write
// inside the helpers; the sites are written only with it on (registration.cpp).
//   DGB   bind the PostProcess attribute dictionary to its SNG song (Rs7Diagram_Bind)
//   DGM   after the colour/handedness finalizer: add the keys (Rs7Diagram_Manifest)
//   DGS   after the loaded-song serializer: add the keys (Rs7Diagram_Loaded)
//   TP13  template lookup of the loaded song: the T8 record (Rs7Ns_TplRecord)
// Full-mode mid-hooks (once per chord diagram or song load), converted from naked stubs (refactor step 7 batch 5,
// frozen at 9c0e44f, tools/tests/test_midhook_features.py). DGB / DGM / DGS call the helper and leave the replay; TP13
// skips it and does the stock lookup itself. The old TP13 stub left the flags of its switch test or lookup; none is
// read before it is rewritten (contract "free").
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_TP13 = 0;

namespace {
// The pointer stored at a game address.
void* DwordPtr(uint32_t address) {
    return Rs7Mid::Ptr<void>(Dword(address));
}
}  // namespace

// DGB - 0x55A02C: PostProcess's UUID attribute lookup (0x8966C0) completed ([ebp+8] = the song, [esp+50h] = the local
// attribute holder). Binds this exact dictionary identity to its SNG song; no matching across songs by shape. Replay:
// `mov esi,[esp+50h]; lea ecx,[esp+1B8h]`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_DGB(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7Diagram_Bind(DwordPtr(c.ebp + 8), DwordPtr(c.esp + 0x50));
}

// DGM - 0x757698: the colour/handedness finalizer completed; all borrowed arrays/args and the newly built dictionary
// are still alive. ESP still includes the finalizer's unpopped argument: object [esp+50h], frets [esp+5Ch], fingers
// [esp+58h], capo byte [esp+3Fh]; attributes [ebp+0Ch], chord [ebp+10h]. Replay: `add esp,4; lea ecx,[esp+74h]`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_DGM(Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    const int capo = Rs7Mid::Byte(c.esp + 0x3F);

    Rs7Diagram_Manifest(DwordPtr(c.esp + 0x50), DwordPtr(c.esp + 0x5C), DwordPtr(c.esp + 0x58), DwordPtr(c.ebp + 0x0C),
                        DwordPtr(c.ebp + 0x10), capo);
}

// DGS - 0x7580DF: all stock fields of the loaded-song serializer finished; holder [ebp+8] / template [ebp+0Ch] are
// borrowed. Replay: `mov ecx,[esp+180h]`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_DGS(Context& c) {
    Rs7Diagram_Loaded(reinterpret_cast<void**>(DwordPtr(c.ebp + 8)),
                      static_cast<const uint8_t*>(DwordPtr(c.ebp + 0x0C)));
}

// TP13 - 0x7578BC (EAX = the template id, ESI = the loaded song's holder): stock `mov edx,[esi+94h]; lea ecx,
// [eax+eax*8]; lea eax,[edx+ecx*8]` = &templates[id]. With diagrams7 the T8 record instead; EDX / ECX keep the stock
// values, and the caller's song retain/release around this block is untouched.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP13(Context& c) {
    const uint32_t id = c.eax, begin = Dword(c.esi + 0x94);

    c.edx = begin;
    c.ecx = id * 9;
    c.eax = g_rs7_diagrams ? Rs7Ns_TplRecord(begin, id) : begin + id * 0x48;
    c.jump = static_cast<uint32_t>(g_rs7_resume_TP13);
}
