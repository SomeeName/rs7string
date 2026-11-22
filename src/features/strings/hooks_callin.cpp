// The calls written into the native GuitarStrings copies (tools/gen_fncopy.py, fncopy_specs.generated.h): call-in
// entries (RsxrMid::CallIn, docs/ARCHITECTURE.md section 14 "Call-in entries") that run these callbacks with the copy's
// registers in place of one stock instruction or call. They replaced the naked Rsxr_Call_* helpers (frozen at 304b9d3,
// tools/tests/test_midhook_callin.py); TP2's callback is the nslot redirect (features/nslot/hooks_records.cpp). Light
// where the copy runs per string per frame and the callback is integer only; Sse for the glow read (an x87 result
// through LoadSt0); full where a callback reaches float or game code.
#include "features/colours/colours.h"
#include "features/nslot/nslot.h"
#include "features/string7/string7.h"
#include "features/strings/hooks.h"
#include "platform/log.h"

using RsxrMid::Byte;
using RsxrMid::Dword;
using RsxrMid::Ptr;
using RsxrMid::Push;

extern "C" {
// GSGL 0x544243 (after the fldz; EBX = renderer): ESI = &glow[0] of the native GuitarStrings (+80h), EDI = -ESI, so
// the ramp's `mov eax,[ebx+4]; add eax,edi; mov ecx,[eax+esi+40h]` reads main[k]. Flags kept (the old `neg` left its
// own, dead at the copy).
__declspec(dllexport) void __cdecl Rsxr_CallIn_GlowSetup(RsxrMid::Context& c) {
    c.esi = Dword(c.ebx + 4) + 0x80;
    c.edi = 0 - c.esi;
}

// GSGL 0x544384 `mov eax,[ecx+eax*4+398h]` (ECX = colour manager, EAX = mode * 0A8h + EDX, EDX = mesh index < 8): the
// state-3 (glow) colour pointer. Native colours: the native manager's state 3 for every slot (mode * 0E0h + EDX at
// +0E30h); else strings 6/7 from g_rsxr_ns_rgb. With g_rsxr_glow_fix strings 0-5 then take Rsxr_GlowFixPtr's answer
// (RSMods' pure red glow, the copy's twin of GLW). Full: Rsxr_GlowFixPtr is float code.
__declspec(dllexport) void __cdecl Rsxr_CallIn_GlowColour(RsxrMid::Context& c) {
    const uint32_t s = c.edx, modeA8 = c.eax - s;

    if (g_rsxr_native_colours) {
        c.eax = Dword(c.ecx + ((modeA8 ? 0xE0u : 0u) + s) * 4 + 0xE30);
    } else if (s >= 6) {
        const uint32_t mode = modeA8 ? 1 : 0;

        c.eax = reinterpret_cast<uintptr_t>(RsxrGs_NsRgb(mode, s, 2));
        return;
    } else {
        c.eax = Dword(c.ecx + c.eax * 4 + 0x398);
    }

    if (g_rsxr_glow_fix && s < 6) {
        c.eax = reinterpret_cast<uintptr_t>(Rsxr_GlowFixPtr(Ptr<const float>(c.eax), Ptr(c.ecx), s));
    }
}

// GSGL 0x5443B3 `fld dword [ebx+edx*4+0ACh]` (glow[mesh index], EBX = renderer): the native glow[8] at +80h of the
// GuitarStrings [EBX+4]. Sse: the callback reads the float with SSE, the thunk does the `fld` after restoring
// (LoadSt0), so the copy gets ST0 as from the stock instruction; per mesh per frame.
__declspec(dllexport) void __cdecl Rsxr_CallIn_GlowRead(RsxrMid::Context& c) {
    (void)RsxrMid::LoadSt0(c, *Ptr<const float>(Dword(c.ebx + 4) + c.edx * 4 + 0x80));
}

// GSHC 0x58E516 `cmp byte [ecx+esi+18h],0` (ECX = s, ESI = headstock, EDI = guitar): s >= 6 is "loaded" while its
// segment exists (ZF = 0, the flags of `test` on the handle), the stock compare otherwise.
__declspec(dllexport) void __cdecl Rsxr_CallIn_HsLoaded(RsxrMid::Context& c) {
    if (c.ecx < 6) {
        c.Compare(Byte(c.ecx + c.esi + 0x18), 0, 8);
        return;
    }

    const uint32_t handle = reinterpret_cast<uintptr_t>(RsxrGs_SegHandle(Ptr(c.edi), c.ecx));

    c.Test(handle, handle, 32);
}

// GSHC 0x58E52D `mov ecx,[esi+ecx*4+20h]; mov ecx,[ecx+0Ch]` -> ECX = the segment's colour param; s >= 6: the
// added segment's handle. Flags kept (stock: `mov`; the old helper left a `cmp`, dead at the copy).
__declspec(dllexport) void __cdecl Rsxr_CallIn_HsSeg(RsxrMid::Context& c) {
    c.ecx = c.ecx < 6 ? Dword(Dword(c.esi + c.ecx * 4 + 0x20) + 0x0C)
                      : reinterpret_cast<uintptr_t>(RsxrGs_SegHandle(Ptr(c.edi), c.ecx));
}

// GSLD 0x59A5E9 `call Rnd_GetStringAttachPoint 0x594530` (ECX = info, cdecl(s, &vec3) -> AL): the stock query rejects
// s > 5; added strings extrapolate from strings 0 and 1 (RsxrGs_AttachExt). Strings 0-5 tail-jump into the game
// function with the copy's registers and stack. Full: RsxrGs_AttachExt is float code (load time).
__declspec(dllexport) void __cdecl Rsxr_CallIn_Attach(RsxrMid::Context& c) {
    const uint32_t s = Dword(c.esp + 4);

    if ((s & 0xFF) < rsxrgs::StockAttachCount(Ptr<const uint8_t>(c.ecx))) {
        c.jump = g_rsxr_fn_attach;
        return;
    }

    const RsxrMid::GameFcw fcw(c);  // RsxrGs_AttachExt calls the game's attach query twice
    const int spaced = RsxrGs_AttachSpaced(Ptr(c.ecx), s, Ptr<float>(Dword(c.esp + 8)));

    c.eax = spaced >= 0 ? uint32_t(spaced) : uint32_t(RsxrGs_AttachExt(Ptr(c.ecx), s, Ptr<float>(Dword(c.esp + 8))));
}

// The return entry of the added strings' shape lookup (g_rsxr_callin_ShapeDone below; RsxrGs_PrepareCallIns).
__declspec(dllexport) uintptr_t g_rsxr_callin_shape_ret = 0;

// GSLD 0x59A15B / 0x59A2EF / 0x59A6A8 `call Rnd_FindStringBaseShape 0x5996A0` (stdcall obj, info, s): the stock
// lookup, then for s >= 6 the added string's atlas row (the stock V table has six). The game function runs with the
// copy's registers and x87 state: strings 0-5 tail-jump into it, strings 6/7 call it on copies of the arguments with
// the return entry g_rsxr_callin_shape_ret as the return address. Light: integer only.
__declspec(dllexport) void __cdecl Rsxr_CallIn_Shape(RsxrMid::Context& c) {
    const uint32_t obj = Dword(c.esp + 4), info = Dword(c.esp + 8), s = Dword(c.esp + 12);

    c.jump = g_rsxr_fn_shape;
    // RsxrGs_PrepareCallIns builds the return entry before any copy exists; without it, the stock lookup alone.
    if ((s & 0xFF) < rsxrgs::StockAttachCount(Ptr<const uint8_t>(info)) || !g_rsxr_callin_shape_ret) {
        return;
    }

    Push(c, s);
    Push(c, info);
    Push(c, obj);
    Push(c, g_rsxr_callin_shape_ret);
}

// The shape lookup of string 6/7 returned (EAX kept): [esp] = the copy's return address, then obj, info, s, which the
// entry's `ret 0Ch` pops. Full: RsxrGs_ShapeDone stores a float.
__declspec(dllexport) void __cdecl Rsxr_CallIn_ShapeDone(RsxrMid::Context& c) {
    RsxrGs_ShapeDone(Ptr(Dword(c.esp + 4)), Dword(c.esp + 12));
}
}

RsxrMid::CallIn g_rsxr_callin_TP2{&Rsxr_CallIn_TP2, RsxrMid::Mode::Light};
RsxrMid::CallIn g_rsxr_callin_GlowSetup{&Rsxr_CallIn_GlowSetup, RsxrMid::Mode::Light};
RsxrMid::CallIn g_rsxr_callin_GlowColour{&Rsxr_CallIn_GlowColour, RsxrMid::Mode::Full};
RsxrMid::CallIn g_rsxr_callin_GlowRead{&Rsxr_CallIn_GlowRead, RsxrMid::Mode::Sse};
RsxrMid::CallIn g_rsxr_callin_HsLoaded{&Rsxr_CallIn_HsLoaded, RsxrMid::Mode::Light};
RsxrMid::CallIn g_rsxr_callin_HsSeg{&Rsxr_CallIn_HsSeg, RsxrMid::Mode::Light};
RsxrMid::CallIn g_rsxr_callin_Attach{&Rsxr_CallIn_Attach, RsxrMid::Mode::Full};
RsxrMid::CallIn g_rsxr_callin_Shape{&Rsxr_CallIn_Shape, RsxrMid::Mode::Light};
RsxrMid::CallIn g_rsxr_callin_ShapeDone{&Rsxr_CallIn_ShapeDone, RsxrMid::Mode::Full, 12};

bool RsxrGs_PrepareCallIns() {
    g_rsxr_callin_shape_ret = g_rsxr_callin_ShapeDone.Entry();

    return g_rsxr_callin_shape_ret != 0;
}
