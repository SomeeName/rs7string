// Chord-diagram glue: the DGB / DGM / DGS callbacks (hooks_stubs.cpp) write the keys through the game's own
// dictionary functions (X_D* refs) and bind each SNG song's attribute dictionary to it.
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <algorithm>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_dkey = 0, g_rs7_dnumber = 0, g_rs7_dset = 0, g_rs7_ddrop = 0;
}

extern "C" __declspec(dllexport) uintptr_t g_rs7_diagrams = 0;

void Rs7Diagram_Configure(const wchar_t* ini) {
    g_rs7_diagrams = Rs7Config_ReadInt(L"chords", L"diagrams7", 1, ini) != 0;
    Rs7Log("config: [chords] diagrams7=%u (default on)", unsigned(g_rs7_diagrams));
    Rs7DiagramMovie_Configure(ini);
}

namespace {
using Method = void*(__thiscall*)(void*);
using Find = void*(__thiscall*)(void*, const char*);
using Ref = void(__thiscall*)(void*);

void RefOp(void* p, int offset) {
    if (p) {
        reinterpret_cast<Ref>((*reinterpret_cast<uintptr_t**>(p))[offset / 4])(p);
    }
}

struct Binding {
    void* song = nullptr;
    void* dictionary = nullptr;
};

Binding g_bindings[kRs7NsEntries];
SRWLOCK g_bindingLock = SRWLOCK_INIT;

void* Dictionary(void* p) {
    return p ? reinterpret_cast<Method>((*reinterpret_cast<uintptr_t**>(p))[0x5c / 4])(p) : nullptr;
}

bool Number(void* node, int* out) {
    if (!node) {
        return false;
    }

    void* number = reinterpret_cast<Method>((*reinterpret_cast<uintptr_t**>(node))[0x54 / 4])(node);
    if (!number) {
        return false;
    }

    double d = *reinterpret_cast<double*>(static_cast<uint8_t*>(number) + 8);
    if (!(d >= -1 && d <= 0x7fffffff) || d != int(d)) {
        return false;
    }

    *out = int(d);

    return true;
}

bool LoadedManifest(void* attributes, void* chord, rs7diagram::Shape* shape) {
    void* dictionary = Dictionary(attributes);
    if (!dictionary || !chord) {
        return false;
    }

    void* node = reinterpret_cast<Find>((*reinterpret_cast<uintptr_t**>(chord))[0x18 / 4])(chord, "ChordId");
    int id = -1;
    if (!Number(node, &id) || id < 0) {
        return false;
    }

    bool ok = false;

    AcquireSRWLockShared(&g_bindingLock);

    for (const auto& b : g_bindings) {
        if (b.dictionary != dictionary) {
            continue;
        }

        for (const auto& e : g_rs7_ns) {
            if (e.song != reinterpret_cast<uintptr_t>(b.song) || !e.tplBegin || !e.t8 || uint32_t(id) >= e.tplCount) {
                continue;
            }

            auto block = Rs7Ext_Find(b.song);
            if (!block || !block->hasTemplates || block->stringCount < 7 || block->stringCount > 8) {
                continue;
            }

            ok = rs7diagram::MergeManifestT8(reinterpret_cast<const uint8_t*>(e.t8 + id * rs7ns::kT8Size), shape);
            break;
        }

        break;
    }

    ReleaseSRWLockShared(&g_bindingLock);

    return ok;
}

// CONFIRMED: same key/numeric/dictionary ABI used at 757313..7573F6.
void GamePut(void* object, const char* text, int value) {
    void* key = nullptr;

    reinterpret_cast<void(__fastcall*)(const char*, void*, void**)>(g_rs7_dkey)(text, nullptr, &key);
    if (!key) {
        return;
    }

    void* number = nullptr;
    double d = value;

    __asm {
        lea edi,number
        sub esp,8
        fld d
        fstp qword ptr [esp]
        call g_rs7_dnumber
        add esp,8
    }

    if (number) {
        reinterpret_cast<void(__stdcall*)(void*, void**, void**)>(g_rs7_dset)(object, &key, &number);
        RefOp(number, 4);
    }

    if (key) {
        __asm mov esi, key __asm call g_rs7_ddrop
    }
}

int ArrayCount(void* array) {
    auto a = static_cast<uintptr_t*>(array);
    if (!a || a[5] < a[4] || (a[5] - a[4]) % 4) {
        return 0;
    }

    return int((a[5] - a[4]) / 4);
}

bool ArrayNumber(void* array, int i, int* value) {
    if (i < 0 || i >= ArrayCount(array)) {
        return false;
    }

    void* node = reinterpret_cast<void**>(static_cast<uintptr_t*>(array)[4])[i];
    if (!node) {
        return false;
    }

    void* num = reinterpret_cast<Method>((*reinterpret_cast<uintptr_t**>(node))[0x54 / 4])(node);
    if (!num) {
        return false;
    }

    double d = *reinterpret_cast<double*>(static_cast<uint8_t*>(num) + 8);
    if (!(d >= -1 && d <= 255) || d != int(d)) {
        return false;
    }

    *value = int(d);

    return true;
}
}  // namespace

extern "C" void __cdecl Rs7Diagram_Loaded(void** holder, const uint8_t* tpl) {
    if (!g_rs7_diagrams || !holder || !*holder) {
        return;
    }

    rs7diagram::Shape shape;
    const uint8_t* t8 = Rs7Ns_AsT8(tpl);

    // Some lesson callers pass a stock-vector pointer directly, bypassing TP13.
    if (!t8) {
        for (const auto& e : g_rs7_ns) {
            uintptr_t p = reinterpret_cast<uintptr_t>(tpl);

            if (e.tplBegin && e.t8 && p >= e.tplBegin && p < e.tplBegin + e.tplCount * rs7ns::kTplSize &&
                (p - e.tplBegin) % rs7ns::kTplSize == 0) {
                t8 = reinterpret_cast<const uint8_t*>(e.t8 + (p - e.tplBegin) / rs7ns::kTplSize * rs7ns::kT8Size);
                break;
            }
        }
    }

    if (rs7diagram::FromT8(t8, &shape)) {
        rs7diagram::Emit(shape, GamePut, *holder);
    }
}

extern "C" void __cdecl Rs7Diagram_Manifest(void* object, void* frets, void* fingers, void* attributes, void* chord,
                                            int capo) {
    if (!g_rs7_diagrams || !object) {
        return;
    }

    rs7diagram::Shape shape;

    shape.count = std::min(ArrayCount(frets), ArrayCount(fingers));
    if (shape.count < 6 || shape.count > 8) {
        return;
    }

    shape.manifest = true;
    shape.capo = capo;

    for (int i = 0; i < shape.count; ++i) {
        if (!ArrayNumber(frets, i, &shape.frets[i]) || !ArrayNumber(fingers, i, &shape.fingers[i])) {
            return;
        }

        if (shape.frets[i] == 255) {
            shape.frets[i] = -1;
        }
    }

    if (shape.count == 6 && !LoadedManifest(attributes, chord, &shape)) {
        return;
    }

    rs7diagram::Emit(shape, GamePut, object);
}

extern "C" void __cdecl Rs7Diagram_Bind(void* song, void* attributes) {
    if (!g_rs7_diagrams || !song) {
        return;
    }

    void* dictionary = Dictionary(attributes);
    if (!dictionary) {
        return;
    }

    AcquireSRWLockExclusive(&g_bindingLock);
    Binding* slot = nullptr;

    for (auto& b : g_bindings) {
        if (b.song == song) {
            slot = &b;
            break;
        }

        if (!b.song && !slot) {
            slot = &b;
        }
    }

    if (slot) {
        RefOp(dictionary, 0);
        RefOp(slot->dictionary, 4);
        *slot = {song, dictionary};
    }

    ReleaseSRWLockExclusive(&g_bindingLock);
}

int Rs7Diagram_Bindings() {
    int n = 0;

    AcquireSRWLockShared(&g_bindingLock);

    for (const auto& b : g_bindings) {
        n += b.song != nullptr;
    }

    ReleaseSRWLockShared(&g_bindingLock);

    return n;
}

void Rs7Diagram_Drop(void* song) {
    AcquireSRWLockExclusive(&g_bindingLock);

    for (auto& b : g_bindings) {
        if (b.song == song) {
            RefOp(b.dictionary, 4);
            b = {};
        }
    }

    ReleaseSRWLockExclusive(&g_bindingLock);
}
