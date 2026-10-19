// The PegSet's C++ mid-hook callbacks (formerly hooks/mid_cold.cpp): each replaced a naked stub on a cold path (the
// PegSet's ctor, load, unload, destructor and free, the renderer's mesh collection, the tuner's colour selection).
// They read the saved context, decide by the ownership table (pegset.h IsNative) and write the stock or the native
// value back. Equivalence against the frozen stubs: tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "features/headstock/assetload.h"
#include "features/headstock/headstock.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegset.h"

using Rs7Mid::Dword;
using Rs7Mid::Push;

// ---- the PegSet's free size, destructor, unload, ctor and load ----
// PSA2 0x58DA05 in the headstock destructor (ESI = PegSet, already destroyed): the sized free's `push
// 15Ch` becomes 1D4h for a native PegSet, which also leaves the ownership table here, before the free.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSA2(Rs7Mid::Context& c) {
    Push(c, rs7head::Disown(reinterpret_cast<const uint8_t*>(c.esi)) ? rs7head::kSize : 0x15C);
    c.jump = uint32_t(g_rs7_resume_PSA2);
}

// PSD1 0x59C30D in the PegSet destructor 0x59C300 (EAX = PegSet): the reverse slot release counter
// `mov [ebp-4],5` is 7 for a native PegSet (8 slots). Flags are dead (lea, then sub at the loop top).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSD1(Rs7Mid::Context& c) {
    *reinterpret_cast<uint32_t*>(c.ebp - 4) = rs7head::IsNative(reinterpret_cast<const uint8_t*>(c.eax)) ? 7 : 5;
    c.jump = uint32_t(g_rs7_resume_PSD1);
}

// PSD2 0x59C314 in 0x59C300 (EAX = PegSet): the release anchor `lea edi,[eax+104h]` (slot 5 + 30h) is
// +154h (slot 7 + 30h) for a native PegSet. Flags dead.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSD2(Rs7Mid::Context& c) {
    c.edi = c.eax + (rs7head::IsNative(reinterpret_cast<const uint8_t*>(c.eax)) ? 0x154 : 0x104);
    c.jump = uint32_t(g_rs7_resume_PSD2);
}

// PSU3 0x59C41D in Rnd_PegSet_Unload 0x59C370 (EDI = PegSet): the taken-flag clear count `mov ecx,6` is 8
// for a native PegSet. Flags dead (the loop's add sets them).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSU3(Rs7Mid::Context& c) {
    c.ecx = rs7head::IsNative(reinterpret_cast<const uint8_t*>(c.edi)) ? 8 : 6;
    c.jump = uint32_t(g_rs7_resume_PSU3);
}

// PSC1 0x59C2F4, the tail of Rnd_PegSet_Ctor 0x59C190 (EDI = PegSet; the stock `mov eax,edi; pop ebx; mov
// esp,ebp` follows): slots 6/7, pegColor[8], tail and registration. The thunk replays the tail.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSC1(Rs7Mid::Context& c) {
    Rs7_PegSetCtor(reinterpret_cast<uint8_t*>(c.edi));
}

// PSL1 0x59C4D0 in Rnd_PegSet_Load 0x59C440 (EDI = instrument info, EBX = PegSet): `movzx eax,byte
// [edi+0Bh]; xor ecx,ecx` = the count the load takes / binds / prepares. A 6-string guitar's native
// PegSet loads g_rs7_peg_load (7) with [neck] headstock_peg on; bass (4) and everything else stock. The
// callback does both stock instructions itself. Flags dead (`xor edx,edx` follows).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSL1(Rs7Mid::Context& c) {
    uint32_t count = *reinterpret_cast<const uint8_t*>(c.edi + 0x0B);

    if (g_rs7_head_on && count == 6 && rs7head::IsNative(reinterpret_cast<const uint8_t*>(c.ebx))) {
        count = uint32_t(g_rs7_peg_load);
    }
    c.eax = count;
    c.ecx = 0;
    c.jump = uint32_t(g_rs7_resume_PSL1);
}

// PSL2 0x59C60C, the call of 0x59CBC0 at the end of a completed Rnd_PegSet_Load (EBX = PegSet, callee-saved; the
// callee pops its PegSet argument, `ret 4`). A call site, After placement (the callee has run, EAX is its result): the
// stock `mov byte [ebx+8],1` (the game repeats it after the return), then Rs7_PegSetLoaded (loaded count kept, active
// count 6, peg 7 culled). Full mode: once per guitar load, on a resource-loader job. Flags are dead. Not a jmp over
// 0x59C611: 0x59C587 `jne 0x59C615` ([ebp+8] != 2) enters the second instruction there, and under `E9 rel32 90 90` that
// is the rel32's top byte - 0x5D for any stub in this DLL (fixed base 0x5E000000) = `pop ebp`: EBP = the saved EDI,
// `mov esp,ebp; pop ebp; ret 8` returned through the caller's EDI object (often to 0) - the issue #18 boot crash.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSL2(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    uint8_t* pegset = reinterpret_cast<uint8_t*>(c.ebx);

    pegset[8] = 1;
    Rs7_PegSetLoaded(pegset);
}

// ---- loops with a conditional branch, the colour selection ----
// PSM2 0x541D34 in Rnd_StringRenderer_Setup 0x541880: the peg mesh collection's pass latch `cmp bl,6; jb
// 0x541B90` (a branch: target = taken). The PegSet is [[ebp+8]+8], as at the loop top. A native PegSet
// compares against 8; the callback sets that compare's flags (they stay exact) and picks the destination.
// Every other PegSet keeps the stock span, which the thunk replays.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSM2(Rs7Mid::Context& c) {
    if (!rs7head::IsNative(reinterpret_cast<const uint8_t*>(Dword(Dword(c.ebp + 8) + 8)))) {
        return;
    }

    c.Compare(c.ebx, 8, 8);
    c.jump = uint32_t((c.ebx & 0xFF) < 8 ? g_rs7_target_PSM2 : g_rs7_resume_PSM2);
}

// PSR1 0x5B0A21 in the peg color_reset 0x5B0960 (ESI = session, EBX = peg index): the loop bound `mov
// ecx,[esi+2Ch]; mov edx,[ecx+24h]; movzx eax,byte [edx+0Bh]` (instrument string count) becomes the
// PegSet's active count word [[[session+2Ch]+18h]+0Ch]+4 (the loop body reads the same PegSet at
// 0x5B09A6). No flags are written, before or after.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSR1(Rs7Mid::Context& c) {
    c.ecx = Dword(c.esi + 0x2C);
    c.edx = Dword(Dword(c.ecx + 0x18) + 0x0C);
    c.eax = *reinterpret_cast<const uint16_t*>(c.edx + 4);
    c.jump = uint32_t(g_rs7_resume_PSR1);
}

// PSN1 0x5B089D in UI_PlayerTuner_SetPegColorAnim 0x5B0880 (EBX = peg index): `mov ecx,[ebx*4 + 0x12ED0C0]`
// reads the six colour names. Index >= 6 (past the table: glyph data) takes name 0, a string-colour name,
// so 0x59BF80 runs its string-colour branch; PSN2 then gives the slot the 7th string's colour. The stub
// compared the index with 6 and left those flags (Compare32 reproduces them).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSN1(Rs7Mid::Context& c) {
    Rs7Head_TrackColourPlayer(reinterpret_cast<uint8_t*>(c.esi));
    const uint32_t* names = reinterpret_cast<const uint32_t*>(g_rs7_pegnames);

    c.Compare32(c.ebx, 6);
    c.ecx = names[c.ebx < 6 ? c.ebx : 0];
    c.jump = uint32_t(g_rs7_resume_PSN1);
}

// ---- refactor step 7 batch 5 (frozen at 9c0e44f, tools/tests/test_midhook_features.py) ----
// PSU1 / PSU2 / PSM1: a slot bound `cmp r16,[pegset+4]` with its branch, 8 for a native PegSet (every slot with an
// entity, also above the active count). The callbacks compare (exact flags) and pick the branch themselves.
namespace {
void SlotBound(Rs7Mid::Context& c, uint32_t slot, uint32_t pegset, uintptr_t below, uintptr_t atOrAbove) {
    const uint32_t bound = rs7head::IsNative(Rs7Mid::Ptr(pegset)) ? 8 : *Rs7Mid::Ptr<const uint16_t>(pegset + 4);

    c.Compare(slot, bound, 16);
    c.jump = static_cast<uint32_t>((slot & 0xFFFF) < bound ? below : atOrAbove);
}
}  // namespace

// PSU1 0x59C3C2 in Rnd_PegSet_Unload 0x59C370 (EDI = PegSet, CX = 0): the give-back loop entry `cmp cx,[edi+4]; jae
// 0x59C41A`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSU1(Rs7Mid::Context& c) {
    SlotBound(c, c.ecx, c.edi, g_rs7_resume_PSU1, g_rs7_target_PSU1);
}

// PSU2 0x59C414 in 0x59C370 (EDI = PegSet): the loop latch `cmp bx,[edi+4]; jb 0x59C3D0`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSU2(Rs7Mid::Context& c) {
    SlotBound(c, c.ebx, c.edi, g_rs7_target_PSU2, g_rs7_resume_PSU2);
}

// PSM1 0x541B99 in Rnd_StringRenderer_Setup 0x541880 (EAX = PegSet, CX = pass): the peg mesh collection's slot bound
// `cmp cx,[eax+4]; jae 0x541D2B`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PSM1(Rs7Mid::Context& c) {
    SlotBound(c, c.ecx, c.eax, g_rs7_resume_PSM1, g_rs7_target_PSM1);
}

// ---- the 7-string model and the tuner letters (group Core) ----
// LT1 0x5B1B53 in UI_PlayerTuner_PlacePegLetters 0x5B1A10 (EDI = InstrumentInfo): `xor al,al; cmp [edi+0Ah],al`
// (+0Ah = the in-line flag, CONFIRMED 0x58ABEC) decides which tuner letters are turned round (3+3: the pegs from
// count/2 on). Rs7Asset_LetterInline answers 1 for a guitar that shows the 7-string in-line headstock on a 3+3 frame,
// so its letters follow the in-line rule; else the stock byte. The answer stays in g_rs7_lt1_inline (the compared byte,
// as before); the flags are that compare's, which the stock `jz` after the site reads.
extern "C" __declspec(dllexport) uintptr_t g_rs7_lt1_inline = 0;

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_LT1(Rs7Mid::Context& c) {
    g_rs7_lt1_inline = static_cast<uint32_t>(Rs7Asset_LetterInline(Rs7Mid::Ptr(c.edi)));
    c.eax &= ~0xFFu;
    c.Compare(g_rs7_lt1_inline & 0xFF, 0, 8);
    c.jump = static_cast<uint32_t>(g_rs7_resume_LT1);
}

// AL1 - entry of the nif memory handler Res_NifMemHandler 0x4A8560 (cdecl (name, buf, size, ctx) -> resource; called
// through the handler map from Res_ParseFileJob_Load 0x4A7AB0 on a loader thread). [assets] headstock7=1: continue in
// the cdecl C wrapper Rs7_NifHandler (same signature, same stack: it calls the stock body through Rs7_NifHandlerStock).
// Otherwise the replay `push ebp; mov ebp,esp; mov eax,[0x135FBFC]`. Flags are dead at a function entry; the old stub
// left its switch test's (contract "dead").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_AL1(Rs7Mid::Context& c) {
    if (g_rs7_asset_on) {
        c.jump = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Rs7_NifHandler));
    }
}
