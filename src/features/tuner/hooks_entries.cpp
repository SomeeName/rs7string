// The native 8-slot tuner's entry and storage mid-hooks (hooks.h), and the [debug] tuner_autotune hook AT. Full mode:
// they run in the tuner only, a few calls per frame (the copy dispatch logs and repairs sessions, AT reads the clock).
// Converted from naked stubs (refactor step 7 batch 5, frozen at 9c0e44f, tools/tests/test_midhook_features.py).
//   TU8H TU8C TU8A  entry dispatch of three GuitarStrings readers: the native copy or the original
//   TS8*            entry dispatch of the 21 UI_PlayerTuner method copies (ids in tuner8s_ids.generated.h)
//   TU8INIT         session constructor tail: the capacity-8 storage of a new session
//   TU8FREE         sized deleting destructor: the session's real size (380h for a native one)
//   AT              UI_PlayerTuner_IsStringTuned entry: "tuned" N seconds after the selection (test driving only)
// An entry dispatch leaves every register and the entry stack as the game left them and continues in the copy or in the
// trampoline Rs7Tuner8_Target / Rs7Tuner8_SessTarget answers (the trampoline is the stock entry bytes + a jump back).
// Entry flags are dead (a call boundary); the old stubs left the helper's, the hooks leave the incoming ones (contract
// "dead"), except AT, whose stock path replays a compare the function reads.
#include "features/tuner/hooks.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8s_ids.generated.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;
using Rs7Mid::Dword;
using Rs7Mid::Ptr;

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_TU8FREE = 0;

namespace {
void ReaderEntry(Context& c, uint32_t kind, uint32_t object) {
    c.jump = static_cast<uint32_t>(Rs7Tuner8_Target(kind, Ptr(object)));
}

// The register frame Rs7Tuner8_SessTarget reads, laid out as the old stub's PUSHAD frame below a reserved slot:
// EDI ESI EBP ESP EBX EDX ECX EAX, the slot, the return address, the entry's [esp+4].
void SessionEntry(Context& c, uint32_t index) {
    const uint32_t regs[11] = {c.edi, c.esi, c.ebp, c.esp - 4,    c.ebx,           c.edx,
                               c.ecx, c.eax, 0,     Dword(c.esp), Dword(c.esp + 4)};

    c.jump = static_cast<uint32_t>(Rs7Tuner8_SessTarget(index, regs));
}
}  // namespace

// TU8H 0x5B0770: session in EAX, string on the stack (`ret 4`).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TU8H(Context& c) {
    ReaderEntry(c, 0, c.eax);
}

// TU8C 0x5B0810: session in ESI (`ret`).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TU8C(Context& c) {
    ReaderEntry(c, 1, c.esi);
}

// TU8A 0x7E7140: StringAction in ECX (`ret 8`).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TU8A(Context& c) {
    ReaderEntry(c, 2, c.ecx);
}

// TS8* (tools/gen_tuner8s.py): one callback per entry site, the spec index selects the method.
#define RS7_TS8_ENTRY(ID, IDX)                                                                                         \
    extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_##ID(Context& c) {                                           \
        SessionEntry(c, IDX);                                                                                          \
    }
RS7_TU8S_SPECS(RS7_TS8_ENTRY)
#undef RS7_TS8_ENTRY

// TU8INIT - 0x5AE305, the constructor's common return `mov eax,esi; pop ebx; mov esp,ebp; pop ebp; ret` (ESI = the
// session; x87 empty since 0x5AE2F6): the native storage, then the replay keeps the stock return value and cleanup.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TU8INIT(Context& c) {
    Rs7Tuner8_InitSession(Ptr(c.esi));
}

// TU8FREE - 0x5AE341 `push 278h`, the size argument of the sized free after the member destructor (ESI = the session):
// the session's real size in the same stack slot. EAX / EDX (the allocator's) are kept.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TU8FREE(Context& c) {
    Rs7Mid::Push(c, Rs7Tuner8_FreeSessionSize(Ptr(c.esi)));
    c.jump = static_cast<uint32_t>(g_rs7_resume_TU8FREE);
}

// AT - entry of UI_PlayerTuner_IsStringTuned 0x5B0C20 (ESI = session; 3 stack arguments, `ret 0Ch`): with [debug]
// tuner_autotune=N "tuned" (AL = 1) N seconds after the selection, with the flags of the old stub's `test eax,eax`;
// otherwise the replayed `push ebp; mov ebp,esp; push ecx; cmp byte [esi+98h],0` (flags exact).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AT(Context& c) {
    if (!g_rs7_tuner_auto) {
        return;
    }

    const uint32_t tuned = Rs7_TunerAutotune(Ptr(c.esi));

    if (!tuned) {
        return;
    }

    Rs7Mid::Return(c, (c.eax & ~0xFFu) | 1, 0x0C);
    c.Test(tuned, tuned, 32);
}
