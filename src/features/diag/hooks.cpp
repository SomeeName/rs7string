// The diagnostic mid-hooks D2, D3 and D5 (written only with [debug] diagnostics=1) and the switch R2 reads.
#include "features/diag/diag.h"
#include "platform/midhook.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_diag = 0;
// Kept exports of the former naked stubs (the rows bind no resume).
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_D2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_D3 = 0;

// CONFIRMED: D2 @0x7A22E0 and D3 @0x7A90F5 (September 2022),
// identical arguments/filtering to the former naked stubs; stock bytes replay
// through the generic relocator. Enabled only by [debug] diagnostics=1.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_D2(Rs7Mid::Context& c) {
    Rs7Diag_FlushNoteHeads();  // R2's queued D1 lines (full mode here)
    const auto* stack = reinterpret_cast<const uint32_t*>(c.esp);
    const uint8_t string = uint8_t(stack[4]);

    if (string == 0 || string == 6) {
        Rs7_DiagGemCreate(stack[0], c.eax, stack[1], stack[3], stack[4], stack[6]);
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_D3(Rs7Mid::Context& c) {
    Rs7Diag_FlushNoteHeads();  // R2's queued D1 lines (full mode here)
    Rs7_DiagGeomBuild(reinterpret_cast<const uint8_t*>(c.edi));
}

// D5 - Rnd_NoteGems_UpdatePositions join (0x7A4E5C): EBX = gem (+0Ch string), the site's ESP + 1Ch = the
// pre-offset position vec3. Logs strings 0 and 6; the thunk replays `fld dword ptr [ebx+14h]; mov eax,[esp+10h]`.
// Per gem, but diagnostics only; the logger uses SSE, so full mode.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_D5(Rs7Mid::Context& c) {
    Rs7Diag_FlushNoteHeads();  // R2's queued D1 lines (full mode here)
    const auto* gem = reinterpret_cast<const uint8_t*>(c.ebx);
    const uint8_t string = gem[0x0C];

    if (string == 0 || string == 6) {
        Rs7_DiagGemPos(gem, reinterpret_cast<const uint32_t*>(c.esp + 0x1C));
    }
}
