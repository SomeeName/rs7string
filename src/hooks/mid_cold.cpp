// C++ mid-hook callbacks for cold sites (see mid_cold.h). Each replaces a naked stub that did
// `[test switch;] pushad; call helper; popad; replay the displaced bytes; jmp resume`. The generic
// thunk saves and restores every register, EFLAGS and x87/SSE state around the callback and replays
// the displaced instructions itself, so a callback only reads the saved context and calls the same C
// helper with the same arguments the stub pushed. Equivalence against the frozen stubs:
// tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "hooks/mid_cold.h"
#include "core/extblock.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/publish.h"
#include "core/string7.h"
#include "core/tuner.h"
#include "core/tuningname.h"

namespace {
// The dword at a game address (a stack slot or a field), as the stubs' `push dword ptr [..]` read it.
inline uint32_t Dword(uint32_t address) {
    return *reinterpret_cast<const uint32_t*>(address);
}

// What `push value` does to the saved context (the thunk writes the edited stack back). Used by the
// callbacks that skip the replay (`jump = resume`) because they push a computed value instead of the
// stock immediate / register.
inline void Push(Rs7Mid::Context& c, uint32_t value) {
    c.esp -= 4;
    *reinterpret_cast<uint32_t*>(c.esp) = value;
}
}  // namespace

// TK - entry of Play_Tuning_ReadStringOffsets 0x5B8480 (ECX = int[6] out, [esp+4] = the holder of the
// target tuning JSON). The native tuner session and the tuning names (TN) need the key per array.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TK(Rs7Mid::Context& c) {
    Rs7_TunerKey(reinterpret_cast<int*>(c.ecx), reinterpret_cast<void*>(Dword(c.esp + 4)));
}

// TS - entry of Play_TunerSession_ApplyTuning 0x5AF200 ([esp+4] = session, EDI = the offsets).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TS(Rs7Mid::Context& c) {
    Rs7_TunerApply(reinterpret_cast<uint8_t*>(Dword(c.esp + 4)), reinterpret_cast<const int*>(c.edi));
}

// TUT - 0x56C408 in 0x56C300: EDI = the arrangement's Tuning object, [ebp-88h] = its Attributes.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TUT(Rs7Mid::Context& c) {
    if (g_rs7_tn_on) {
        Rs7_TnUuidTuning(reinterpret_cast<void*>(c.edi), reinterpret_cast<void*>(Dword(c.ebp - 0x88)));
    }
}

// XB1 - 0x55922C at the end of SNG_Song_Load 0x558BD0 (loader thread), after the last MetaData read and
// before the body's memory stream is destroyed: song = [EBP+8], stream = the site's ESP + 14h.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_XB1(Rs7Mid::Context& c) {
    Rs7Ext_OnSngLoaded(reinterpret_cast<void*>(Dword(c.ebp + 8)), reinterpret_cast<const void*>(c.esp + 0x14));
}

// ---- pool request counts (Rnd_Guitar_LoadDependencies) ----
// N1 0x5965CA `movzx eax,[eax+38h]; push ecx; push 1` (EAX = profile, the type-4 string-line request
// count) and N21 0x5965E9 `movzx ecx,[ecx+38h]; push eax; push 1` (the type-2 ghost-string count). A
// count of 6 or 7 becomes 8 with the native_publish ports (RefreshCounts preloads all 8 GuitarStrings
// slots, one instance per loaded string); otherwise 6 becomes 7 while [neck] string7_line is set; bass
// (4) and everything else stay. The stub did the movzx and both pushes itself, so the callback skips the
// replay. Flags are dead (`xor dl,dl` follows); the old stub left its own comparison flags.
namespace {
uint32_t RequestCount(uint32_t count) {
    if (count < 6 || count > 7) {
        return count;
    }
    if (g_rs7_publish_ports) {
        return 8;
    }

    return count == 6 && g_rs7_s7_on ? 7 : count;
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N1(Rs7Mid::Context& c) {
    c.eax = RequestCount(*reinterpret_cast<const uint8_t*>(c.eax + 0x38));
    Push(c, c.ecx);
    Push(c, 1);
    c.jump = uint32_t(g_rs7_resume_N1);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N21(Rs7Mid::Context& c) {
    c.ecx = RequestCount(*reinterpret_cast<const uint8_t*>(c.ecx + 0x38));
    Push(c, c.eax);
    Push(c, 1);
    c.jump = uint32_t(g_rs7_resume_N21);
}

// ---- native GuitarStrings / InstrumentInfo free sizes (guitar release 0x592770) ----
// GSZ1 0x59280A `push 40h; push 3; push esi` (ESI = GuitarStrings): the native size 0B0h for a native
// GuitarStrings, whose marker and magic are cleared so a recycled block never looks native; else 40h.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GSZ1(Rs7Mid::Context& c) {
    uint32_t size = rs7gs::kStockGsSize;
    uint8_t* const gs = reinterpret_cast<uint8_t*>(c.esi);

    if (rs7gs::IsNative(gs)) {
        *reinterpret_cast<uint16_t*>(gs + rs7gs::kMarkerOff) = 0;
        *reinterpret_cast<uint32_t*>(gs + rs7gs::kMagicOff) = 0;
        size = rs7gs::kGsSize;
    }

    Push(c, size);
    Push(c, 3);
    Push(c, c.esi);
    c.jump = uint32_t(g_rs7_resume_GSZ1);
}

// GIZ1 0x592824 `push 38h; push 3; push esi` (ESI = InstrumentInfo): Rs7Gs_InfoFreeSize unregisters a
// native one and returns its size. The stub left that result in EAX.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GIZ1(Rs7Mid::Context& c) {
    c.eax = Rs7Gs_InfoFreeSize(reinterpret_cast<uint8_t*>(c.esi));
    Push(c, c.eax);
    Push(c, 3);
    Push(c, c.esi);
    c.jump = uint32_t(g_rs7_resume_GIZ1);
}

// ---- native N-slot PegSet ----
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

// ---- native PegSet / GuitarStrings loops with a conditional branch ----
// PSM2 0x541D34 in Rnd_StringRenderer_Setup 0x541880: the peg mesh collection's pass latch `cmp bl,6; jb
// 0x541B90` (a branch: target = taken). The PegSet is [[ebp+8]+8], as at the loop top. A native PegSet
// compares against 8; the callback sets that compare's flags (they stay exact) and picks the destination.
// Every other PegSet keeps the stock span, which the thunk replays. (PSU1, PSU2 and PSM1 stay hand-written:
// their displaced `cmp r16,[mem]` carries an operand-size prefix the relocator does not accept.)
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

// GSSS 0x7DFDBB in Play_Session_OnSongStart 0x7DFB50: `xor al,al; cmp [edx+0Bh],al; jbe 0x7DFDD4` (ECX =
// GuitarStrings, EDX = its info, EBX = session). A native GuitarStrings of this info with its load complete
// (+0A3h) sets the section dimming flag +70h on its loaded strings (7th too, so it dims with the others
// whenever it is shown) and joins at 0x7DFDD4; the stub's loop left EAX = the count, ESI = the last string
// and the flags of its closing `cmp edi,eax`, which the callback reproduces. Anything else: the stock span.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GSSS(Rs7Mid::Context& c) {
    const uint8_t* const gs = reinterpret_cast<const uint8_t*>(c.ecx);
    if (!rs7gs::IsNative(gs) || Dword(c.ecx + 0xA8) != c.edx || gs[0xA3] == 0) {
        return;
    }

    const uint32_t count = *reinterpret_cast<const uint8_t*>(c.edx + 0x41);

    for (uint32_t i = 0; i < count; ++i) {
        c.esi = Dword(c.ecx + 0x40 + 4 * i);
        *reinterpret_cast<uint8_t*>(c.esi + 0x70) = 1;
    }

    c.eax = count;
    c.Compare32(count, count);
    c.jump = uint32_t(g_rs7_target_GSSS);
}

// GSSE 0x7E005E in Play_Session_OnSongEnd_ClearStringDimming 0x7E0040: `cmp [edx+0Bh],bl; jbe 0x7E0074`
// (ECX = GuitarStrings, EDX = info, BL = 0). A native GuitarStrings clears +70h (to BL) on all 8 strings it
// has; the stub's loop left EAX = 8, EDI = the last slot's pointer and the flags of `cmp eax,8`.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GSSE(Rs7Mid::Context& c) {
    if (!rs7gs::IsNative(reinterpret_cast<const uint8_t*>(c.ecx))) {
        return;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        c.edi = Dword(c.ecx + 0x40 + 4 * i);
        if (c.edi) {
            *reinterpret_cast<uint8_t*>(c.edi + 0x70) = uint8_t(c.ebx);
        }
    }

    c.eax = 8;
    c.Compare32(8, 8);
    c.jump = uint32_t(g_rs7_target_GSSE);
}
