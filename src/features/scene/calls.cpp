// Shared register-ABI calls moved from the strings and headstock glue; inline assembly preserves
// the existing EAX entity convention and the game callees' differing stack cleanup.
#include "features/scene/scene.h"
#include "features/string7/string7.h"

void rsxrs7d::ShowEntity(uint8_t* entity, uint32_t show) {
    if (!g_rsxr_fn_show) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        push show
        mov eax, entity
        call dword ptr [g_rsxr_fn_show]
        add esp, 4
        pop edi
        pop esi
        pop ebx
    }
}

void rsxrs7d::FadeEntity(uintptr_t fn, uint8_t* entity, float seconds) {
    const uint32_t mgr = g_rsxr_fade_mgr ? *reinterpret_cast<const uint32_t*>(g_rsxr_fade_mgr) : 0;
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
