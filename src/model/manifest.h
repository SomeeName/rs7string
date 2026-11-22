#pragma once
#include "platform/seh.h"
#include <windows.h>
#include <cstdint>

// A manifest key on one of the game's JSON objects (the Tuning object, the attributes), read through the game's own
// JSON vtable as 0x56C950 / 0x889C50 / Play_Tuning_ReadStringOffsets 0x5B84DC do: object vtable +14h find(name) ->
// node or 0, node vtable +50h -> number object or 0, the double at +8. One reader for the tuner and C-lite.
// tools/tests/test_manifest_readkey.py compiles this header with the tuner's ReadKey on its own.
namespace rsxr {
using JsonFind = void*(__thiscall*)(void* self, const char* name);
using JsonNum = uint8_t*(__thiscall*)(void* self);

// The number under key. False on a missing key, a node that is not a number or any fault.
inline bool JsonNumber(void* obj, const char* key, double* out) {
    __try {
        if (!obj) {
            return false;
        }

        void** vt = *static_cast<void***>(obj);
        void* node = reinterpret_cast<JsonFind>(vt[0x14 / 4])(obj, key);
        if (!node) {
            return false;
        }

        void** nvt = *static_cast<void***>(node);
        uint8_t* num = reinterpret_cast<JsonNum>(nvt[0x50 / 4])(node);
        if (!num) {
            return false;
        }

        *out = *reinterpret_cast<const double*>(num + 8);
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

// A tuning offset: a number in (-25, 25), truncated as the stock readers do (0xF5B0D0).
inline bool JsonOffset(void* obj, const char* key, int* out, double* raw = nullptr) {
    double d = 0.0;
    if (!JsonNumber(obj, key, &d) || !(d > -25.0 && d < 25.0)) {
        return false;
    }

    *out = static_cast<int>(d);
    if (raw) {
        *raw = d;
    }

    return true;
}

// True when key holds a tuning offset (JsonOffset's test, the value unused).
inline bool HasOffset(void* obj, const char* key) {
    int unused = 0;

    return JsonOffset(obj, key, &unused);
}
}  // namespace rsxr
