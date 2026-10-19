// Entry dispatch of the publication's function copies (hooks.h): with the publication ports on, six stock functions run
// as relocated copies that read the published count or the eight-entry tuning tail (tools/gen_publish.py,
// gen_publish_pitch.py); their entries choose the copy or the original (the trampoline: the stock entry bytes + a jump
// back). Every register, the flags and the entry stack stay as the game left them; live x87 state stays live.
//   PUBCL PUBTO PUBWU   copies of 0x8750C0 (tracker, ECX), 0x4E58E0 (matcher, ESI), 0x58E790 (the caller's FretGrid,
//                       EDI): Rs7Publish_Target(kind, register)
//   PUBNL PUBAN PUBAC   copies of 0x4E6E90 (matcher), 0x875430 (tracker), 0x8755C0 (chord-mistake template; ECX = the
//                       template): Rs7PublishPitch_Target(kind, the entry's [esp+4])
// Light mid-hooks (the detection path calls these per note; integer-only targets, LIGHT_CALLBACKS in
// tools/tests/emu_harness.py), converted from naked stubs (refactor step 7 batch 5, frozen at 9c0e44f,
// tools/tests/test_midhook_features.py). The old stubs kept the entry flags (pushfd / popfd); so does the thunk.
#include "features/publish/hooks.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;

namespace {
void Entry(Context& c, unsigned kind, uint32_t object) {
    c.jump = static_cast<uint32_t>(Rs7Publish_Target(kind, Rs7Mid::Ptr(object)));
}

void PitchEntry(Context& c, unsigned kind) {
    const uint32_t object = Rs7Mid::Dword(c.esp + 4);

    c.jump = static_cast<uint32_t>(Rs7PublishPitch_Target(kind, Rs7Mid::Ptr(object)));
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBCL(Context& c) {
    Entry(c, 0, c.ecx);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBTO(Context& c) {
    Entry(c, 1, c.esi);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBWU(Context& c) {
    Entry(c, 2, c.edi);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBNL(Context& c) {
    PitchEntry(c, 0);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBAN(Context& c) {
    PitchEntry(c, 1);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PUBAC(Context& c) {
    PitchEntry(c, 2);
}
