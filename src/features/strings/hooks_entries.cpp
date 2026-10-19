// Mid-hooks of the native N-slot GuitarStrings (strings.h, issue #18 stage 4): the entry dispatch of the 16 copied
// functions and the renderer's mesh collection.
//   GS<xx> entries   the stock function runs as a native copy (gen_fncopy.py) for a native GuitarStrings, the stock
//   code
//                    otherwise: Rs7Gs_Target(kind, the GuitarStrings at the entry or 0, [esp+4], [esp+8]) answers the
//                    copy or the trampoline (the stock entry bytes + a jump back); every register and the entry stack
//                    stay as the game left them
//   GRM1 GRM2        Rnd_StringRenderer_Setup 0x541880: main-string countdown 6 -> loaded count; main string from the
//                    native array
// Full mode (a few calls per frame and guitar; the dispatch logs and guards with SEH), converted from naked stubs
// (refactor step 7 batch 5, frozen at 9c0e44f, tools/tests/test_midhook_features.py). Entry flags are dead (a call
// boundary) and the old stubs left the helper's; GRM1 / GRM2 left theirs where the next instruction rewrites them
// (contracts "dead" / "free").
#include "features/strings/hooks.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_GRM1 = 0, g_rs7_resume_GRM2 = 0;

namespace {
// Where the GuitarStrings is at a copied function's entry.
enum class Obj : uint8_t { Eax, Edi, Esi, Arg1, None };

void Entry(Context& c, uint32_t kind, Obj where) {
    const uint32_t a1 = Dword(c.esp + 4), a2 = Dword(c.esp + 8);
    uint32_t gs = 0;

    switch (where) {
        case Obj::Eax:
            gs = c.eax;
            break;
        case Obj::Edi:
            gs = c.edi;
            break;
        case Obj::Esi:
            gs = c.esi;
            break;
        case Obj::Arg1:
            gs = a1;
            break;
        case Obj::None:
            break;
    }

    c.jump = static_cast<uint32_t>(Rs7Gs_Target(kind, Rs7Mid::Ptr(gs), a1, a2));
}
}  // namespace

// ID, kind (index into Rs7GsKind), where the GuitarStrings is at the entry, the stock function.
#define RS7_GS_ENTRY(ID, KIND, WHERE)                                                                                  \
    extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_##ID(Context& c) {                                           \
        Entry(c, KIND, Obj::WHERE);                                                                                    \
    }
RS7_GS_ENTRY(GSFR, 0, Eax)    // Rnd_GuitarStrings_Free 0x599D70
RS7_GS_ENTRY(GSBD, 1, Arg1)   // 0x599E20
RS7_GS_ENTRY(GSUL, 2, Arg1)   // unload 0x599EA0
RS7_GS_ENTRY(GSLD, 3, Arg1)   // Rnd_GuitarStrings_Load 0x599F90 (GuitarStrings, context)
RS7_GS_ENTRY(GSFO, 4, Edi)    // FadeOut 0x59A720
RS7_GS_ENTRY(GSFI, 5, Edi)    // FadeIn 0x59A820
RS7_GS_ENTRY(GSXF, 6, Arg1)   // UpdateTransforms 0x59A920
RS7_GS_ENTRY(GSWD, 7, Edi)    // 0x59AA60
RS7_GS_ENTRY(GSUP, 8, Esi)    // Update 0x59AAE0
RS7_GS_ENTRY(GSRS, 9, Edi)    // ResetState 0x59AC20
RS7_GS_ENTRY(GSPK, 10, Arg1)  // Pluck 0x59AD30
RS7_GS_ENTRY(GSVB, 11, Arg1)  // 0x59AD80
RS7_GS_ENTRY(GSIC, 12, Arg1)  // InitColors 0x59ADC0 (GuitarStrings, info)
RS7_GS_ENTRY(GSDM, 13, None)  // UpdateSectionDimming 0x460550 (stack context)
RS7_GS_ENTRY(GSGL, 14, None)  // Rnd_StringRenderer_UpdateGlow 0x5441A0 (stack renderer)
RS7_GS_ENTRY(GSHC, 15, None)  // headstock segment colours 0x58E4F0 (stack headstock, guitar)
#undef RS7_GS_ENTRY

// GRM1 - 0x541D44 `mov dword [ebp-14h],6` (the main-string mesh countdown; [ebp+8] = renderer, +4 = GuitarStrings): the
// loaded count of a native GuitarStrings (7 for a guitar), else 6.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GRM1(Context& c) {
    const uint32_t gs = Dword(Dword(c.ebp + 8) + 4);

    *reinterpret_cast<uint32_t*>(c.ebp - 0x14) = Rs7Gs_MeshCount(Rs7Mid::Ptr(gs));
    c.jump = static_cast<uint32_t>(g_rs7_resume_GRM1);
}

// GRM2 - 0x541D54 `mov eax,[ecx+eax+4]; mov eax,[eax+4]` (EAX = GuitarStrings, ECX = 4*s): main[s] from the native
// array (+40h) of a native GuitarStrings; its entity.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GRM2(Context& c) {
    const bool native = rs7gs::IsNative(Rs7Mid::Ptr(c.eax));
    const uint32_t main = Dword(c.ecx + c.eax + (native ? 0x40 : 0x04));

    c.eax = Dword(main + 4);
    c.jump = static_cast<uint32_t>(g_rs7_resume_GRM2);
}
