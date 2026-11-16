#include "features/frets/frets.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegset.h"
#include "features/scene/scene.h"
#include "features/string7/string7.h"
#include "platform/seh.h"
#include <windows.h>

// The game functions the PegSet glue and the model's peg placement call (hooks.h). Game thread.

namespace {
constexpr float kRadToDegNeg = -57.2957763671875f;  // 0x12246D8 (double), used by 0x59C67D..0x59C6A5
constexpr uint32_t kWorldRot = 0x80;                // NiAVObject world rotation (3x3)
constexpr uint32_t kWorldPos = 0xA4;                // NiAVObject world translation

// X_EULER of a 3x3 rotation, in degrees as Rnd_PegSet_ApplyTransforms 0x59C630 scales it (0x59C67D..0x59C6A5).
void GameEuler(const uint8_t* rot, float* e) {  // 0x8FCAA0 (radians, then scaled like 0x59C680..)
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
}  // namespace

void rs7head::GameShowEntity(uint8_t* entity, uint32_t show) {
    if (entity) {
        rs7s7d::ShowEntity(entity, show);
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
    rs7s7d::FadeEntity(fn, entity, seconds);
}

bool rs7head::GamePlacePegs(uint8_t* pegset, unsigned count) {
    if (!pegset || !g_rs7_fn_setpos || !g_rs7_fn_setrot || !g_rs7_fn_euler) {
        return false;
    }

    using SetVec = void(__cdecl*)(uint8_t*, const float*);

    __try {
        for (unsigned s = 0; s < count && s < unsigned(kCapacity); ++s) {
            uint8_t* slot = Slot(pegset, int(s));
            uint8_t* peg = UncheckedField<uint8_t*>(slot, kSlotEntity);
            uint8_t* loc = UncheckedField<uint8_t*>(slot, kSlotLocator);
            if (!peg || !loc || !slot[0]) {
                continue;
            }

            float e[3] = {};

            GameEuler(loc + kWorldRot, e);
            reinterpret_cast<SetVec>(g_rs7_fn_setpos)(peg, reinterpret_cast<const float*>(loc + kWorldPos));
            reinterpret_cast<SetVec>(g_rs7_fn_setrot)(peg, e);
        }

        return true;
    } __except (Rs7_FaultFilter(GetExceptionCode())) {
        return false;
    }
}
