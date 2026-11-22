#include "features/scene/scene.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <excpt.h>

// SEH-guarded game-memory helpers shared by the native strings, frets, pegs and headstock model. Layouts (Gamebryo
// objects of the September 2022 exe): NiObjectNET +0x10 name (fixed string = char*); GetRTTI is vtbl+8 (NiRTTI: +0
// name, +4 base).

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_fn_world = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_root = 0;
}

namespace {
constexpr uint32_t kName = 0x10;
// Offsets read by Rnd_FindStringBaseShape 0x5996C4..0x5996EE.
constexpr uint32_t kWorldReady = 0x10C;  // world: the lookup bails out while this is zero
constexpr uint32_t kEntityFlags = 0xD8;  // entity: bit 0 set -> root node is stored at kEntityRoot
constexpr uint32_t kEntityRoot = 0xDC;   // entity: root scene node (valid when kEntityFlags bit 0 is set)

// ---- SEH-guarded memory access and game calls (no C++ objects in these frames) ----

void SafeStr(const char* p, char* out, size_t cap) {
    if (!cap) {
        return;
    }

    size_t i = 0;

    __try {
        for (; p && i + 1 < cap && p[i]; ++i) {
            out[i] = p[i];
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }

    out[i] = 0;
}

const uint8_t* Rtti(const void* obj) {
    if (!obj) {
        return nullptr;
    }

    __try {
        void* const* vt = *static_cast<void* const* const*>(obj);
        using Fn = const uint8_t*(__fastcall*)(const void*, void*);  // thiscall, no stack args

        return reinterpret_cast<Fn>(vt[2])(obj, nullptr);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

uint8_t* CallWorld() {
    if (!g_rsxr_fn_world) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<uint8_t*(__cdecl*)()>(g_rsxr_fn_world)();
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

uint8_t* CallRoot(uint8_t* entity, uint8_t* world) {
    if (!g_rsxr_fn_root) {
        return nullptr;
    }

    __try {
        using Fn = uint8_t*(__fastcall*)(uint8_t*, void*, uint8_t*);  // ECX = entity, (world) ret 4

        return reinterpret_cast<Fn>(g_rsxr_fn_root)(entity, nullptr, world);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}
}  // namespace

namespace rsxrs7d {
// The entity's root scene node, exactly as Rnd_FindStringBaseShape 0x5996C4..0x5996EE finds it.
uint8_t* EntityRoot(uint8_t* entity) {
    if (!entity) {
        return nullptr;
    }

    uint8_t* world = CallWorld();
    if (!world || !Rsxr_SafeRead<uint32_t>(world, kWorldReady)) {
        return nullptr;
    }
    if (Rsxr_SafeRead<uint32_t>(entity, kEntityFlags) & 1) {
        return Rsxr_SafeRead<uint8_t*>(entity, kEntityRoot);
    }

    return CallRoot(entity, world);
}

bool IsKindOf(const void* obj, const char* kind) {
    const uint8_t* r = Rtti(obj);

    for (int i = 0; r && i < 16; ++i) {
        char buf[48];

        SafeStr(Rsxr_SafeRead<const char*>(r, 0), buf, sizeof buf);
        if (std::strcmp(buf, kind) == 0) {
            return true;
        }

        r = Rsxr_SafeRead<const uint8_t*>(r, 4);
    }

    return false;
}

void KindOf(const void* obj, char* out, size_t cap) {
    if (!cap) {
        return;
    }

    out[0] = 0;
    if (const uint8_t* r = Rtti(obj)) {
        SafeStr(Rsxr_SafeRead<const char*>(r, 0), out, cap);
    }
    if (!out[0]) {
        std::snprintf(out, cap, "?");
    }
}

void NameOfNode(const void* obj, char* out, size_t cap) {
    SafeStr(Rsxr_SafeRead<const char*>(obj, kName), out, cap);
}
}  // namespace rsxrs7d
