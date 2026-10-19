#include "features/scene/scene.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <excpt.h>

// SEH-guarded game-memory helpers shared by the native strings, frets, pegs and headstock model (formerly the
// render diagnostics and mirroring of the stage-1 7th string object S7, removed in 1.0). Layouts (Gamebryo objects of
// the September 2022 exe): NiObjectNET +0x10 name (fixed string = char*); GetRTTI is vtbl+8 (NiRTTI: +0 name,
// +4 base).

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_world = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_root = 0;
}

namespace {
constexpr uint32_t kName = 0x10;

// ---- SEH-guarded memory access and game calls (no C++ objects in these frames) ----
bool SafeRead(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(dst, 0, n);
        return false;
    }
}

bool SafeWrite(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
T Rd(const void* base, uint32_t off) {
    T v{};

    if (base) {
        SafeRead(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
    }

    return v;
}

void SafeStr(const char* p, char* out, size_t cap) {
    size_t i = 0;

    __try {
        for (; p && i + 1 < cap && p[i]; ++i) {
            out[i] = p[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void KindName(const void* obj, char* out, size_t cap) {
    out[0] = 0;
    if (const uint8_t* r = Rtti(obj)) {
        SafeStr(Rd<const char*>(r, 0), out, cap);
    }
    if (!out[0]) {
        std::snprintf(out, cap, "?");
    }
}

bool IsKind(const void* obj, const char* name) {
    const uint8_t* r = Rtti(obj);

    for (int i = 0; r && i < 16; ++i) {
        char buf[48];

        SafeStr(Rd<const char*>(r, 0), buf, sizeof buf);
        if (std::strcmp(buf, name) == 0) {
            return true;
        }

        r = Rd<const uint8_t*>(r, 4);
    }

    return false;
}

void NameOf(const void* obj, char* out, size_t cap) {
    SafeStr(Rd<const char*>(obj, kName), out, cap);
}

uint8_t* CallWorld() {
    if (!g_rs7_fn_world) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<uint8_t*(__cdecl*)()>(g_rs7_fn_world)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

uint8_t* CallRoot(uint8_t* entity, uint8_t* world) {
    if (!g_rs7_fn_root) {
        return nullptr;
    }

    __try {
        using Fn = uint8_t*(__fastcall*)(uint8_t*, void*, uint8_t*);  // ECX = entity, (world) ret 4

        return reinterpret_cast<Fn>(g_rs7_fn_root)(entity, nullptr, world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// The entity's root scene node, exactly as Rnd_FindStringBaseShape 0x5996C4..0x5996EE finds it.
uint8_t* RootOf(uint8_t* entity) {
    if (!entity) {
        return nullptr;
    }

    uint8_t* world = CallWorld();
    if (!world || !Rd<uint32_t>(world, 0x10C)) {
        return nullptr;
    }
    if (Rd<uint32_t>(entity, 0xD8) & 1) {
        return Rd<uint8_t*>(entity, 0xDC);
    }

    return CallRoot(entity, world);
}
}  // namespace

namespace rs7s7d {
uint8_t* EntityRoot(uint8_t* entity) {
    return RootOf(entity);
}

bool SafeCopy(void* dst, const void* src, size_t n) {
    return SafeRead(dst, src, n);
}

bool IsKindOf(const void* obj, const char* kind) {
    return IsKind(obj, kind);
}

void KindOf(const void* obj, char* out, size_t cap) {
    KindName(obj, out, cap);
}

void NameOfNode(const void* obj, char* out, size_t cap) {
    NameOf(obj, out, cap);
}

bool SafeStore(void* dst, const void* src, size_t n) {
    return SafeWrite(dst, src, n);
}
}  // namespace rs7s7d
