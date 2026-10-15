// Native N-slot GuitarStrings stubs (issue #18 stage 4, core/gsnative.h). Every site here belongs to the native
// strings (registry: kRs7StrNative), a member of the native 7-string unit; the stage-1 7th string object (N2-N4,
// N6, N7, N11, N14-N17) was removed in 1.0.
//
// Contents, in file order (all in the game's Rnd_GuitarStrings / Rnd_Guitar / renderer code):
//   GS<xx> entries   16 entry dispatchers: the stock function runs as a native copy (gen_fncopy.py) for a
//                    native GuitarStrings, the stock code otherwise
//   GSA1 GSC1 GSZ1   allocation, construction and sized free of the GuitarStrings (0xB0 bytes)
//   GIA1 GIZ1        allocation and sized free of the InstrumentInfo (0x48 bytes)
//   GRM1-GRM3        renderer mesh collection: count 8, native array, atlas row of strings 6/7
//   GSSS GSSE        song start / end: section dimming flag on / off for every loaded string
//   Rs7_Call_*       calls written INTO the relocated copies (not sites): what a copy runs in place of one
//                    stock instruction or call
#include "core/gsnative.h"
#include "hooks/stub_macros.h"
#include <cstdint>

static_assert(rs7gs::kMarker == 0x3752 && rs7gs::kMarkerOff == 0x3A && rs7gs::kMagic == 0x47375352 &&
                  rs7gs::kMagicOff == 0xAC,
              "RS7_GS_IF_NATIVE repeats these constants (hooks/stub_macros.h)");

extern "C" {
extern uintptr_t g_rs7_fn_alloc;
extern uintptr_t g_rs7_fn_shape;
extern uintptr_t g_rs7_colormgr;
extern uintptr_t g_rs7_native_colours;
void Rs7_NsTplAt();
// Rs7Gs_Use's answer -> the address the entry stub jumps to (copy or trampoline).
__declspec(dllexport) uintptr_t __cdecl Rs7Gs_Target(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2);
}

#define RS7_FNEXT(id) extern "C" uintptr_t g_rs7_fncopy_##id, g_rs7_fntramp_##id;
RS7_FNEXT(GSFR)
RS7_FNEXT(GSBD)
RS7_FNEXT(GSUL)
RS7_FNEXT(GSLD)
RS7_FNEXT(GSFO)
RS7_FNEXT(GSFI)
RS7_FNEXT(GSXF)
RS7_FNEXT(GSWD)
RS7_FNEXT(GSUP)
RS7_FNEXT(GSRS)
RS7_FNEXT(GSPK)
RS7_FNEXT(GSVB)
RS7_FNEXT(GSIC)
RS7_FNEXT(GSDM)
RS7_FNEXT(GSGL)
RS7_FNEXT(GSHC)
#undef RS7_FNEXT

namespace {
struct Pair {
    uintptr_t* copy;
    uintptr_t* tramp;
};

#define RS7_P(id) {&g_rs7_fncopy_##id, &g_rs7_fntramp_##id}
// Indexed by Rs7GsKind.
const Pair kByKind[kGsKinds] = {RS7_P(GSFR), RS7_P(GSBD), RS7_P(GSUL), RS7_P(GSLD), RS7_P(GSFO), RS7_P(GSFI),
                                RS7_P(GSXF), RS7_P(GSWD), RS7_P(GSUP), RS7_P(GSRS), RS7_P(GSPK), RS7_P(GSVB),
                                RS7_P(GSIC), RS7_P(GSDM), RS7_P(GSGL), RS7_P(GSHC)};
#undef RS7_P
}  // namespace

extern "C" uintptr_t __cdecl Rs7Gs_Target(uint32_t kind, uint8_t* reg, uint32_t a1, uint32_t a2) {
    const Pair& p = kByKind[kind];

    return (*p.copy && Rs7Gs_Use(kind, reg, a1, a2)) ? *p.copy : *p.tramp;
}

// ---- entry dispatch of the copied functions ----
// At the function entry (flags dead, x87 empty): RS7_ENTRY_DISPATCH_ARGS (hooks/stub_macros.h) asks
// Rs7Gs_Target (kind, the GuitarStrings register or 0, [esp+4], [esp+8]) and `ret`s into the answer
// with the entry stack and registers unchanged: the native copy or the trampoline (the stock entry
// bytes + jmp back into the stock function). Each line: ID, kind (index into Rs7GsKind), where the
// GuitarStrings is at the entry, the stock function.
#define RS7_GS_ENTRY(ID, KIND, REG) RS7_ENTRY_DISPATCH_ARGS(ID, Rs7Gs_Target, KIND, REG)
RS7_GS_ENTRY(GSFR, 0, eax)                   // Rnd_GuitarStrings_Free 0x599D70 (EAX = GS)
RS7_GS_ENTRY(GSBD, 1, dword ptr[esp + 48])   // 0x599E20 (stack GS): [esp+4] at entry = [esp+48] after 3 pushes
RS7_GS_ENTRY(GSUL, 2, dword ptr[esp + 48])   // unload 0x599EA0 (stack GS)
RS7_GS_ENTRY(GSLD, 3, dword ptr[esp + 48])   // Rnd_GuitarStrings_Load 0x599F90 (stack GS, context)
RS7_GS_ENTRY(GSFO, 4, edi)                   // FadeOut 0x59A720 (EDI = GS)
RS7_GS_ENTRY(GSFI, 5, edi)                   // FadeIn 0x59A820 (EDI = GS)
RS7_GS_ENTRY(GSXF, 6, dword ptr[esp + 48])   // UpdateTransforms 0x59A920 (stack GS)
RS7_GS_ENTRY(GSWD, 7, edi)                   // 0x59AA60 (EDI = GS)
RS7_GS_ENTRY(GSUP, 8, esi)                   // Update 0x59AAE0 (ESI = GS)
RS7_GS_ENTRY(GSRS, 9, edi)                   // ResetState 0x59AC20 (EDI = GS)
RS7_GS_ENTRY(GSPK, 10, dword ptr[esp + 48])  // Pluck 0x59AD30 (stack GS)
RS7_GS_ENTRY(GSVB, 11, dword ptr[esp + 48])  // 0x59AD80 (stack GS)
RS7_GS_ENTRY(GSIC, 12, dword ptr[esp + 48])  // InitColors 0x59ADC0 (stack GS, info)
RS7_GS_ENTRY(GSDM, 13, 0)                    // UpdateSectionDimming 0x460550 (stack context)
RS7_GS_ENTRY(GSGL, 14, 0)                    // Rnd_StringRenderer_UpdateGlow 0x5441A0 (stack renderer)
RS7_GS_ENTRY(GSHC, 15, 0)                    // headstock segment colours 0x58E4F0 (stack headstock, guitar)
#undef RS7_GS_ENTRY
// (After `sub esp,4` + pushad the entry [esp+4] is at [esp+40]; the two argument pushes move it to
// [esp+48], which the REG push reads before it moves.)

// ---- owners: the guitar ctor 0x5925F0 and its release 0x592770 ----
RS7_RESUME_VAR(GSA1);
RS7_RESUME_VAR(GSC1);
RS7_TARGET_VAR(GSC1);
RS7_RESUME_VAR(GSZ1);
RS7_RESUME_VAR(GIA1);
RS7_TARGET_VAR(GIA1);
RS7_RESUME_VAR(GIZ1);

// GSA1 - GuitarStrings allocation size (0x40 -> 0xB0).
// 0x592642 `push 40h; call 0x8F9D40` (the GuitarStrings allocation; [esp] = the 0 argument
// pushed before): the native size. The caller's `add esp,8` follows at the resume.
RS7_STUB(Rs7_Stub_GSA1) {
    __asm {
        push 0B0h
        call dword ptr [g_rs7_fn_alloc]
        jmp g_rs7_resume_GSA1
    }
}

// GSC1 - native layout after the stock GuitarStrings constructor.
// 0x592650 `push eax; call Rnd_GuitarStrings_Ctor 0x599CE0` (EAX = the allocation, ESI = the
// guitar): the stock ctor, then the native layout (Rs7Gs_Construct). EAX = the GuitarStrings.
RS7_STUB(Rs7_Stub_GSC1) {
    __asm {
        push eax
        push esi
        call Rs7Gs_Construct
        add esp, 8
        jmp g_rs7_resume_GSC1
    }
}

// GSZ1 (0x59280A, GuitarStrings free size) and GIZ1 (0x592824, InstrumentInfo free size) use C++
// mid-hook callbacks (mid_cold.cpp).

// GIA1 - InstrumentInfo allocation size (0x38 -> 0x48).
// 0x592622 `push 38h; mov [esi+4Ch],ebx; call 0x8F9D40` (the InstrumentInfo allocation): the
// native size and its extension (Rs7Gs_InfoAllocated) before the stock ctor runs.
RS7_STUB(Rs7_Stub_GIA1) {
    __asm {
        push 48h
        mov [esi+4Ch], ebx
        call dword ptr [g_rs7_target_GIA1]
        test eax, eax
        jz gia_done
        pushad
        push eax
        call Rs7Gs_InfoAllocated
        add esp, 4
        popad
    gia_done:
        jmp g_rs7_resume_GIA1
    }
}

// ---- renderer mesh collection, Rnd_StringRenderer_Setup 0x541880 ----
RS7_RESUME_VAR(GRM1);
RS7_RESUME_VAR(GRM2);
RS7_RESUME_VAR(GRM3);

// GRM1 - renderer mesh collection: main-string countdown 6 -> loaded count.
// 0x541D44 `mov dword [ebp-14h],6` (the main-string mesh countdown; [ebp+8] = renderer, +4 =
// GuitarStrings): the loaded count of a native GuitarStrings (7 for a guitar), else 6.
RS7_STUB(Rs7_Stub_GRM1) {
    __asm {
        pushad
        mov eax, [ebp+8]
        push dword ptr [eax+4]
        call Rs7Gs_MeshCount
        add esp, 4
        mov [ebp-14h], eax
        popad
        jmp g_rs7_resume_GRM1
    }
}

// GRM2 - renderer mesh collection: main string from the native array.
// 0x541D54 `mov eax,[ecx+eax+4]; mov eax,[eax+4]` (EAX = GuitarStrings, ECX = 4*s): main[s]
// from the native array (+40h) of a native GuitarStrings; its entity.
RS7_STUB(Rs7_Stub_GRM2) {
    RS7_GS_IF_NATIVE(eax, grm2_stock)
    __asm {
        mov eax, [ecx+eax+40h]
        jmp grm2_tail
    grm2_stock:
        mov eax, [ecx+eax+4]
    grm2_tail:
        mov eax, [eax+4]
        jmp g_rs7_resume_GRM2
    }
}

// GRM3 - renderer mesh collection: atlas V row of strings 6/7.
// 0x541EE5 `mov edx,[ebp+10h]; fld dword [edx+1199D9Ch]` (the guitar V row of mesh s, EDX =
// 4*s): strings 6/7 read g_rs7_ns_v (the stock table has six rows).
RS7_STUB(Rs7_Stub_GRM3) {
    __asm {
        mov edx, [ebp+10h]
        cmp edx, 18h
        jae grm3_ext
        fld dword ptr [edx+1199D9Ch]
        jmp g_rs7_resume_GRM3
    grm3_ext:
        fld dword ptr [g_rs7_ns_v + edx - 18h]
        jmp g_rs7_resume_GRM3
    }
}

// ---- song start / end: section dimming switched on / off per string (+70h) ----
RS7_RESUME_VAR(GSSS);
RS7_TARGET_VAR(GSSS);
RS7_TARGET_VAR(GSSE);

// GSSS (0x7DFDBB, Play_Session_OnSongStart: section dimming on) and GSSE (0x7E005E, Play_Session_OnSongEnd:
// dimming cleared) use C++ mid-hook callbacks (mid_cold.cpp).

// ---- calls written into the copies (tools/gen_fncopy.py); each ends with ret ----

// GSDM 0x4608AC (TP2 in the stock function): `lea edx,[edi+edi*8]; lea edi,[ebx+edx*8]` -> EDI = the
// T8 template record (Rs7_NsTplAt: EAX begin, EDX id -> EAX).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_TP2() {
    __asm {
        push eax
        mov eax, ebx
        mov edx, edi
        call Rs7_NsTplAt
        lea edx, [edi+edi*8]
        mov edi, eax
        pop eax
        ret
    }
}

// GSGL 0x544243 (after the fldz; EBX = renderer): ESI = &glow[0] of the native GuitarStrings (+80h),
// EDI = -ESI, so the ramp's `mov eax,[ebx+4]; add eax,edi; mov ecx,[eax+esi+40h]` reads main[k].
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_GlowSetup() {
    __asm {
        mov esi, [ebx+4]
        add esi, 80h
        mov edi, esi
        neg edi
        ret
    }
}

// GSGL 0x544384 `mov eax,[ecx+eax*4+398h]` (ECX = colour manager, EAX = mode*0A8h + EDX, EDX = mesh
// index < 8): the state-3 (glow) colour pointer; 6/7 from g_rs7_ns_rgb.
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_GlowColour() {
    __asm {
        cmp dword ptr [g_rs7_native_colours], 0
        jne glc_native
        cmp edx, 6
        jae glc_ext
        mov eax, [ecx+eax*4+398h]
        ret
    glc_ext:
        sub eax, edx  // mode * 0A8h
        neg eax
        sbb eax, eax
        neg eax  // mode 0 / 1
        lea eax, [eax*2+edx-6]  // mode * 2 + (s - 6)
        lea eax, [eax+eax*2]
        add eax, 2  // state 3 (glow)
        lea eax, [eax+eax*2]
        lea eax, g_rs7_ns_rgb[eax*4]
        ret
    glc_native:
        // The GSGL copy retains its stock mode*A8 arithmetic. Convert it once
        // here to the native mode*E0 index, then read native state3 for ALL slots.
        sub eax, edx
        test eax, eax
        jz glc_mode0
        mov eax, 0E0h
    glc_mode0:
        add eax, edx
        mov eax, [ecx+eax*4+3B0h]
        ret
    }
}

// GSGL 0x5443B3 `fld dword [ebx+edx*4+0ACh]` (glow[mesh index]): the native glow[8] (+80h).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_GlowRead() {
    __asm {
        push eax
        mov eax, [ebx+4]
        fld dword ptr [eax+edx*4+80h]
        pop eax
        ret
    }
}

// GSHC 0x58E516 `cmp byte [ecx+esi+18h],0` (ECX = s, ESI = headstock, EDI = guitar): s >= 6 is
// "loaded" while its segment exists (ZF = 0), the stock flag otherwise.
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_HsLoaded() {
    __asm {
        cmp ecx, 6
        jae hsl_ext
        cmp byte ptr [ecx+esi+18h], 0
        ret
    hsl_ext:
        push eax
        push ecx
        push edx
        push ecx
        push edi
        call Rs7Gs_SegHandle
        add esp, 8
        test eax, eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// GSHC 0x58E52D `mov ecx,[esi+ecx*4+20h]; mov ecx,[ecx+0Ch]` -> ECX = the segment's colour param.
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_HsSeg() {
    __asm {
        cmp ecx, 6
        jae hss_ext
        mov ecx, [esi+ecx*4+20h]
        mov ecx, [ecx+0Ch]
        ret
    hss_ext:
        push eax
        push edx
        push ecx
        push edi
        call Rs7Gs_SegHandle
        add esp, 8
        mov ecx, eax
        pop edx
        pop eax
        ret
    }
}

// GSLD 0x59A5E9 `call Rnd_GetStringAttachPoint 0x594530` (ECX = info, cdecl(s, &vec3) -> AL): the
// stock query rejects s > 5; added strings extrapolate from strings 0 and 1 (Rs7Gs_AttachExt).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_Attach() {
    __asm {
        cmp byte ptr [esp+4], 6
        jae att_ext
        jmp dword ptr [g_rs7_fn_attach]
    att_ext:
        push ecx
        push dword ptr [esp+12]
        push dword ptr [esp+12]
        push ecx
        call Rs7Gs_AttachExt
        add esp, 16
        ret
    }
}

// GSLD 0x59A15B / 0x59A2EF / 0x59A6A8 `call Rnd_FindStringBaseShape 0x5996A0` (stdcall obj, info, s):
// the stock lookup, then for s >= 6 the added string's atlas row (the stock V table has six).
extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Call_Shape() {
    __asm {
        cmp byte ptr [esp+12], 6
        jae shp_ext
        jmp dword ptr [g_rs7_fn_shape]
    shp_ext:
        push dword ptr [esp+12]
        push dword ptr [esp+12]
        push dword ptr [esp+12]
        call dword ptr [g_rs7_fn_shape]
        push eax
        push dword ptr [esp+16]
        push dword ptr [esp+12]
        call Rs7Gs_ShapeDone
        add esp, 8
        pop eax
        ret 12
    }
}
