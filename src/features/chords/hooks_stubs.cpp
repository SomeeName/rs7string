// The chord frame's string-axis span: the naked stub B2 (sites.json), exported with its link variables so
// tools/tests/test_emu.py can run it in Unicorn. B1 (marks the frames after Rnd_GemGeom_Ctor) is a C++ mid-hook in
// hooks_frames.cpp.
#include "features/chords/chords.h"
#include <cstdint>

extern "C" __declspec(dllexport) uintptr_t g_rs7_target_B2 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_fn_sst = 0;

// B2 - the call of Rnd_GemGeom_ApplySpan 0x7AEE80 at 0x7A91E4 in Rnd_GemGeom_Build (ECX = component
// +20h vec3, EDX = +2Ch vec3, [esp+4] = the model entity, caller cleans; EDI = component; x87 empty).
// A component B1 marked gets the stock span scale/translation plus the frame's string-axis scale
// (Rs7Chord_BoxScale fills g_rs7_box_scale / g_rs7_box_trans), set with the stock
// Rnd_Gem_SetScaleTranslate 0x7AFC00 (EAX = entity; X_SST) and the entity's vfunc +8, exactly as
// ApplySpan does. Everything else: stock ApplySpan.
// Stays asm: replaces the callee with calls of game functions in their own conventions (X_SST takes EAX; the vfunc),
// which a C++ callback cannot make.
extern "C" __declspec(dllexport) float g_rs7_box_scale[3] = {1.0f, 1.0f, 1.0f};
extern "C" __declspec(dllexport) float g_rs7_box_trans[3] = {0.0f, 0.0f, 0.0f};

extern "C" __declspec(dllexport) __declspec(naked) void Rs7_Stub_B2() {
    __asm {
        pushad
        push edi
        call Rs7Chord_BoxScale
        add esp, 4
        test eax, eax
        popad
        jz b2_stock
        mov eax, [esp+4]
        test eax, eax
        jz b2_done
        push offset g_rs7_box_trans
        push offset g_rs7_box_scale
        call g_rs7_fn_sst
        add esp, 8
        mov ecx, [esp+4]
        mov eax, [ecx]
        call dword ptr [eax+8]
    b2_done:
        ret
    b2_stock:
        jmp g_rs7_target_B2
    }
}
