#include "features/frets/frets.h"
#include "features/headstock/hooks.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"

// The game functions the PegSet glue and the model's peg placement call (hooks.h). Game thread.

namespace {
constexpr float kRadToDegNeg = -57.2957763671875f;  // 0x12246D8 (double), used by 0x59C67D..0x59C6A5
}  // namespace

void rs7head::GameShowEntity(uint8_t* entity, uint32_t show) {
    if (!g_rs7_fn_show || !entity) {
        return;
    }

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

void rs7head::GamePegSetCall(uintptr_t fn, uint8_t* pegset) {
    if (!fn || !pegset) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push pegset
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

// As the PegSet fade loops 0x59C95C / 0x59C89C call them: EAX = entity, (ClipFade manager, seconds, 0, 1.0) ret 10h.
void rs7head::GameFade(uintptr_t fn, uint8_t* entity, float seconds) {
    uint8_t* mgr = g_rs7_fade_mgr ? *reinterpret_cast<uint8_t**>(g_rs7_fade_mgr) : nullptr;
    if (!fn || !mgr || !entity) {
        return;
    }

    const float zero = 0.0f, one = 1.0f;

    __asm {
        push ebx
        push esi
        push edi
        push one
        push zero
        push seconds
        push mgr
        mov eax, entity
        call dword ptr [fn]
        pop edi
        pop esi
        pop ebx
    }
}

void rs7head::GameEuler(const uint8_t* rot, float* e) {  // 0x8FCAA0 (radians, then scaled like 0x59C680..)
    float* e0 = e;
    float* e1 = e + 1;
    float* e2 = e + 2;

    __asm {
        push ebx
        push esi
        push edi
        mov esi, rot
        mov edi, e2
        push e1
        push e0
        call dword ptr [g_rs7_fn_euler]
        pop edi
        pop esi
        pop ebx
    }

    for (int i = 0; i < 3; ++i) e[i] *= kRadToDegNeg;
}

void rs7head::GameSetVec(uintptr_t fn, uint8_t* entity, const float* v) {
    using Fn = void(__cdecl*)(uint8_t*, const float*);

    reinterpret_cast<Fn>(fn)(entity, v);
}

void rs7head::GameSetScaleU(uint8_t* entity, float k) {  // EAX = entity, cdecl(float)
    __asm {
        push ebx
        push esi
        push edi
        push k
        mov eax, entity
        call dword ptr [g_rs7_fn_setscale]
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
}
