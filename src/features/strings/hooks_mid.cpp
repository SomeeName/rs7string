// Mid-hook callbacks of the native GuitarStrings (hooks.h): the free sizes of the GuitarStrings and InstrumentInfo
// (guitar release) and the two section-dimming loops at song start / end. They replaced hand-written naked stubs
// (docs/ARCHITECTURE.md, "Mid-hook conversion"); equivalence against the frozen stubs:
// tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "features/strings/hooks.h"

using Rs7Mid::Dword;
using Rs7Mid::Push;

extern "C" {
// The link variables of the sites (hooks.h; GRM1 / GRM2: hooks_entries.cpp), bound by Rs7_Startup.
__declspec(dllexport) uintptr_t g_rs7_target_GSA1 = 0, g_rs7_resume_GRM3 = 0, g_rs7_resume_GSZ1 = 0,
                                g_rs7_resume_GIZ1 = 0, g_rs7_resume_GSSS = 0, g_rs7_target_GSSS = 0,
                                g_rs7_target_GSSE = 0;
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

// ---- GuitarStrings loops with a conditional branch ----
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

// ---- the guitar ctor 0x5925F0: allocations and the GuitarStrings ctor (call sites; stubs frozen at b72bc52,
// tools/tests/test_midhook_wrap_sites.py) ----
// GSA1 - the call of the allocator 0x8F9D40 at 0x592644 after `push 40h` (cdecl (size, 0); the caller's `add esp,8`
// follows): Before placement, the size argument becomes the native 0B0h and the allocator runs on it.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GSA1(Rs7Mid::Context& c) {
    *Rs7Mid::Ptr<uint32_t>(c.esp + 4) = rs7gs::kGsSize;
}

// ---- renderer mesh collection, Rnd_StringRenderer_Setup 0x541880 (once per guitar setup) ----
// GRM3 0x541EE5 `mov edx,[ebp+10h]; fld dword [edx+1199D9Ch]` (the guitar V row of mesh s, EDX = 4*s): strings 6/7
// (EDX >= 18h) load g_rs7_ns_v instead (the stock table has six rows); the others replay the stock bytes. The old stub
// compared EDX with 18h and left those flags (Compare32). On a full x87 stack (never at this site) the replay runs.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GRM3(Rs7Mid::Context& c) {
    const uint32_t row = Dword(c.ebp + 0x10);

    c.Compare32(row, 0x18);
    if (row < 0x18 || !c.PushSt0(g_rs7_ns_v[(row - 0x18) / 4])) {
        return;
    }

    c.edx = row;
    c.jump = uint32_t(g_rs7_resume_GRM3);
}
