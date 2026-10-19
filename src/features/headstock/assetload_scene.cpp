#include "features/headstock/assetload_scene.h"
#include <windows.h>
#include <cstring>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_fn_fixstr = 0;
}

namespace rs7asset {
uint8_t* TopObject(const void* res) {
    uint8_t* stream = Rd<uint8_t*>(res, kResStream);
    if (!stream || Rd<uint32_t>(stream, kStreamTopCount) < 1) {
        return nullptr;
    }

    uint8_t** base = Rd<uint8_t**>(stream, kStreamTop);

    return base ? Rd<uint8_t*>(base, 0) : nullptr;
}

// Game calls, each SEH-guarded (a fault leaves the stock model alone).
const char* MakeFixedString(const char* text) {  // 0x91CBF0, cdecl(const char*) -> handle (+1 ref)
    if (!g_rs7_fn_fixstr) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<const char*(__cdecl*)(const char*)>(g_rs7_fn_fixstr)(text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void Rename(uint8_t* node, const char* to) {
    if (!node) {
        return;
    }

    if (const char* fs = MakeFixedString(to)) {
        rs7s7d::SafeStore(node + kName, &fs, sizeof fs);
    }
}

uint8_t* FindAlt(uint8_t* root) {  // breadth-first, first 3 levels
    uint8_t* q[64];
    int head = 0, tail = 0;

    q[tail++] = root;

    for (int depth = 0; depth < 3 && head < tail; ++depth) {
        const int end = tail;

        for (; head < end; ++head) {
            uint8_t* n = q[head];
            if (!rs7s7d::IsKindOf(n, "NiNode")) {
                continue;
            }

            uint8_t* found = nullptr;

            EachChild(n, [&](uint8_t* c) {
                char name[16] = {};

                rs7s7d::NameOfNode(c, name, sizeof name);

                if (!std::strcmp(name, kAltName)) {
                    found = c;
                    return false;
                }

                if (tail < 64) {
                    q[tail++] = c;
                }

                return true;
            });

            if (found) {
                return found;
            }
        }
    }

    return nullptr;
}

uint8_t* FindByName(uint8_t* root, const char* want) {  // depth-first, at most 256 nodes
    uint8_t* stack[64];
    int sp = 0, seen = 0;

    stack[sp++] = root;

    while (sp && seen++ < 256) {
        uint8_t* n = stack[--sp];
        char name[48] = {};

        rs7s7d::NameOfNode(n, name, sizeof name);
        if (!std::strcmp(name, want)) {
            return n;
        }
        if (!rs7s7d::IsKindOf(n, "NiNode")) {
            continue;
        }

        const unsigned slots = Rd<uint16_t>(n, kChildSlots);
        uint8_t** kids = Rd<uint8_t**>(n, kChildren);

        for (int i = int(slots < 48 ? slots : 48) - 1; kids && i >= 0; --i) {
            uint8_t* c = Rd<uint8_t*>(kids, 4 * uint32_t(i));

            if (c && sp < 64) {
                stack[sp++] = c;
            }
        }
    }

    return nullptr;
}
}  // namespace rs7asset

int rs7asset::GamePropType(const uint8_t* prop) {
    if (!prop) {
        return -1;
    }

    using TypeFn = int(__thiscall*)(const uint8_t*);

    __try {
        const uintptr_t vt = *reinterpret_cast<const uintptr_t*>(prop);

        return (*reinterpret_cast<const TypeFn*>(vt + 0x48))(prop);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

uint8_t* rs7asset::FindProperty(const uint8_t* obj, int type, PropTypeFn typeOf) {
    if (!obj || !typeOf) {
        return nullptr;
    }

    __try {
        const uint8_t* node = *reinterpret_cast<const uint8_t* const*>(obj + kPropList);

        for (int i = 0; node && i < 64; ++i) {
            uint8_t* prop = *reinterpret_cast<uint8_t* const*>(node + 8);
            if (prop && typeOf(prop) == type) {
                return prop;
            }

            node = *reinterpret_cast<const uint8_t* const*>(node);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return nullptr;
}

int rs7asset::CopyLayer(const uint8_t* from, uint8_t* to, PropTypeFn typeOf, uint16_t* value, uint16_t* was) {
    const uint8_t* src = FindProperty(from, kLayerType, typeOf);
    uint8_t* dst = FindProperty(to, kLayerType, typeOf);
    uint16_t v = 0, old = 0;

    if (src && !rs7s7d::SafeCopy(&v, src + kLayerValue, sizeof v)) {
        src = nullptr;
    }
    if (src && value) {
        *value = v;
    }
    if (!src || !dst || !rs7s7d::SafeCopy(&old, dst + kLayerValue, sizeof old)) {
        return -1;
    }
    if (old == v) {
        return 0;
    }
    if (!rs7s7d::SafeStore(dst + kLayerValue, &v, sizeof v)) {
        return -1;
    }

    if (was) {
        *was = old;
    }

    return 1;
}
