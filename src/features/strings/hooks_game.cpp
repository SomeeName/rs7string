// Calls into the game for the native GuitarStrings (hooks.h), and the two answers the copies' calls need from it:
// the attach point and the atlas row of an added string (the call-ins Rs7_CallIn_Attach / Rs7_CallIn_Shape,
// hooks_callin.cpp).
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "features/strings/hooks.h"
#include "platform/log.h"
#include <cstring>

using namespace rs7gs;

uint8_t* rs7gs::GameAlloc(uint32_t size) {
    using Fn = uint8_t*(__cdecl*)(uint32_t, uint32_t);

    return reinterpret_cast<Fn>(g_rs7_fn_alloc)(size, 0);
}

void rs7gs::GameStrCtor(uint8_t* obj, uint32_t kind) {
    __asm {
        push ebx
        push esi
        push edi
        mov esi, obj
        mov eax, kind
        call dword ptr [g_rs7_fn_strctor]
        pop edi
        pop esi
        pop ebx
    }
}

void rs7gs::GameShow(uint8_t* entity, uint32_t show) {
    __asm {
        push ebx
        push esi
        push edi
        push show
        mov eax, entity
        call dword ptr [g_rs7_fn_show]
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
}

void rs7gs::GameDetach(uint8_t* entity, uint32_t detach) {
    using Fn = uint32_t(__cdecl*)(uint8_t*, uint32_t, uint32_t);

    reinterpret_cast<Fn>(g_rs7_fn_detach)(entity, detach, 1);
}

void rs7gs::GameFade(uint8_t* entity, bool in, float seconds) {
    const uintptr_t fn = in ? g_rs7_fn_fadein : g_rs7_fn_fadeout;
    const uint32_t mgr = g_rs7_fade_mgr ? *reinterpret_cast<const uint32_t*>(g_rs7_fade_mgr) : 0;
    if (!fn || !mgr || !entity) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push 3F800000h
        push 0
        push seconds
        push mgr
        mov eax, entity
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

namespace {
// Rnd_GetStringAttachPoint 0x594530: ECX = info, cdecl(s, &vec3) -> AL.
bool GameAttach(uint8_t* info, uint32_t s, float* outVec) {
    uint32_t ok = 0;

    __asm {
        push ebx
        push esi
        push edi
        push outVec
        push s
        mov ecx, info
        call dword ptr [g_rs7_fn_attach]
        add esp, 8
        movzx eax, al
        mov ok, eax
        pop edi
        pop esi
        pop ebx
    }

    return ok != 0;
}
}  // namespace

extern "C" int __cdecl Rs7Gs_AttachExt(uint8_t* info, uint32_t s, float* out) {
    s &= 0xFF;
    float a0[3], a1[3];

    std::memcpy(a0, out, sizeof a0);
    std::memcpy(a1, out, sizeof a1);
    if (!GameAttach(info, 0, a0) || !GameAttach(info, 1, a1)) {
        return 0;
    }

    ExtrapolateAttach(a0, a1, s, out);  // s 6: 2*a0 - a1, s 7: 3*a0 - 2*a1
    if (Upto(&g_strings.logAttach, 2)) {
        Rs7Log("gsnative: string %u attach (%.3f, %.3f, %.3f) from strings 0/1", s, double(out[0]), double(out[1]),
               double(out[2]));
    }

    return 1;
}

extern "C" void __cdecl Rs7Gs_ShapeDone(uint8_t* obj, uint32_t s) {
    s &= 0xFF;
    if (!obj || !Rd<uint8_t>(obj, 0x50) || s < 6 || s > 7) {
        return;
    }

    if (uint8_t* h = Rd<uint8_t*>(obj, 0x3C)) {
        At<float>(h, 0x14) = g_rs7_ns_v[s - 6];
    }
    if (Upto(&g_strings.logShape, 2)) {
        Rs7Log("gsnative: string %u (%s %p) shader params found, atlas row %.2f", s,
               Rd<uint32_t>(obj, 0) ? "ghost" : "main", obj, double(g_rs7_ns_v[s - 6]));
    }
}
