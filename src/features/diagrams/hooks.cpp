// Chord-diagram glue: the DGB / DGM / DGS callbacks (hooks_mid.cpp) write the keys through the game's own
// dictionary functions (X_D* refs) and bind each SNG song's attribute dictionary to it.
#include "features/diagrams/diagrams.h"
#include "features/nslot/nslot.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <algorithm>

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_dkey = 0, g_rsxr_dnumber = 0, g_rsxr_dset = 0, g_rsxr_ddrop = 0;
}

extern "C" __declspec(dllexport) uintptr_t g_rsxr_diagrams = 0;

void RsxrDiagram_Configure(const wchar_t* ini) {
    g_rsxr_diagrams = RsxrConfig_ReadInt(L"chords", L"diagrams7", 1, ini) != 0;
    RsxrLog("config: [chords] diagrams7=%u (default on)", unsigned(g_rsxr_diagrams));
    RsxrDiagramMovie_Configure(ini);
}

namespace {
using Method = void*(__thiscall*)(void*);
using Find = void*(__thiscall*)(void*, const char*);
using Ref = void(__thiscall*)(void*);

// AddRef (0) / Release (4) through the object's vtable. Guarded: RsxrDiagram_Bind / _Drop call it while holding
// g_bindingLock, and a fault caught further up (RsxrMid_Guard) would leave the lock held.
void RefOp(void* p, int offset) {
    if (!p) {
        return;
    }

    __try {
        reinterpret_cast<Ref>((*reinterpret_cast<uintptr_t**>(p))[offset / 4])(p);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
    }
}

struct Binding {
    void* song = nullptr;
    void* dictionary = nullptr;
};

Binding g_bindings[kRsxrNsEntries];
SRWLOCK g_bindingLock = SRWLOCK_INIT;

// The game objects below are reached from mid-hook callbacks (asm thunks, no SEH frame): every vtable call and raw
// read sits in an __try so a stale or foreign object yields "not found" instead of a crash.
void* Dictionary(void* p) {
    if (!p) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<Method>((*reinterpret_cast<uintptr_t**>(p))[0x5c / 4])(p);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// A dictionary node's number object (node vtable +54h); null for a null node or a fault.
void* NodeNumberObject(void* node) {
    if (!node) {
        return nullptr;
    }

    __try {
        return reinterpret_cast<Method>((*reinterpret_cast<uintptr_t**>(node))[0x54 / 4])(node);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// The chord's "ChordId" node (chord vtable +18h); null for a fault.
void* ChordIdNode(void* chord) {
    __try {
        return reinterpret_cast<Find>((*reinterpret_cast<uintptr_t**>(chord))[0x18 / 4])(chord, "ChordId");
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return nullptr;
    }
}

// A dictionary node's number (node vtable +54h, the double at +8) as an integer in [-1, max].
bool NodeNumber(void* node, double max, int* out) {
    void* number = NodeNumberObject(node);
    if (!number) {
        return false;
    }

    double d = 0;
    if (!Rsxr_SafeCopy(&d, static_cast<uint8_t*>(number) + 8, sizeof d)) {
        return false;
    }

    if (!(d >= -1 && d <= max) || d != int(d)) {
        return false;
    }

    *out = int(d);

    return true;
}

bool LoadedManifest(void* attributes, void* chord, rsxrdiagram::Shape* shape) {
    void* dictionary = Dictionary(attributes);
    if (!dictionary || !chord) {
        return false;
    }

    void* node = ChordIdNode(chord);
    int id = -1;
    if (!NodeNumber(node, 0x7fffffff, &id) || id < 0) {
        return false;
    }

    bool ok = false;

    AcquireSRWLockShared(&g_bindingLock);

    for (const auto& b : g_bindings) {
        if (b.dictionary != dictionary) {
            continue;
        }

        for (const auto& e : g_rsxr_ns) {
            if (e.song != reinterpret_cast<uintptr_t>(b.song) || !e.tplBegin || !e.t8 || uint32_t(id) >= e.tplCount) {
                continue;
            }

            auto block = RsxrExt_Find(b.song);
            if (!block || !block->hasTemplates || block->stringCount < 7 || block->stringCount > 8) {
                continue;
            }

            ok = rsxrdiagram::MergeManifestT8(reinterpret_cast<const uint8_t*>(e.t8 + id * rsxrns::kT8Size), shape);
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

    reinterpret_cast<void(__fastcall*)(const char*, void*, void**)>(g_rsxr_dkey)(text, nullptr, &key);
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
        call g_rsxr_dnumber
        add esp,8
    }

    if (number) {
        reinterpret_cast<void(__stdcall*)(void*, void**, void**)>(g_rsxr_dset)(object, &key, &number);
        RefOp(number, 4);
    }

    if (key) {
        __asm mov esi, key __asm call g_rsxr_ddrop
    }
}

int ArrayCount(void* array) {
    uintptr_t a[6] = {};
    if (!array || !Rsxr_SafeCopy(a, array, sizeof a) || a[5] < a[4] || (a[5] - a[4]) % 4) {
        return 0;
    }

    return int((a[5] - a[4]) / 4);
}

bool ArrayNumber(void* array, int i, int* value) {
    if (i < 0 || i >= ArrayCount(array)) {
        return false;
    }

    const auto begin = Rsxr_SafeRead<uintptr_t>(array, 4 * sizeof(uintptr_t));

    return NodeNumber(Rsxr_SafeRead<void*>(reinterpret_cast<const void*>(begin), size_t(i) * sizeof(void*)), 255,
                      value);
}
}  // namespace

extern "C" void __cdecl RsxrDiagram_Loaded(void** holder, const uint8_t* tpl) {
    if (!g_rsxr_diagrams || !holder || !*holder) {
        return;
    }

    rsxrdiagram::Shape shape;
    const uint8_t* t8 = RsxrNs_AsT8(tpl);

    // Some lesson callers pass a stock-vector pointer directly, bypassing TP13. This mirrors RsxrNs_TplRecord
    // (features/nslot/hooks.cpp) in reverse: stock template address -> T8 record.
    if (!t8) {
        for (const auto& e : g_rsxr_ns) {
            uintptr_t p = reinterpret_cast<uintptr_t>(tpl);

            if (e.tplBegin && e.t8 && p >= e.tplBegin && p < e.tplBegin + e.tplCount * rsxrns::kTplSize &&
                (p - e.tplBegin) % rsxrns::kTplSize == 0) {
                t8 = reinterpret_cast<const uint8_t*>(e.t8 + (p - e.tplBegin) / rsxrns::kTplSize * rsxrns::kT8Size);
                break;
            }
        }
    }

    if (rsxrdiagram::FromT8(t8, &shape)) {
        rsxrdiagram::Emit(shape, GamePut, *holder);
    }
}

extern "C" void __cdecl RsxrDiagram_Manifest(void* object, void* frets, void* fingers, void* attributes, void* chord,
                                             int capo) {
    if (!g_rsxr_diagrams || !object) {
        return;
    }

    rsxrdiagram::Shape shape;

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

    rsxrdiagram::Emit(shape, GamePut, object);
}

extern "C" void __cdecl RsxrDiagram_Bind(void* song, void* attributes) {
    if (!g_rsxr_diagrams || !song) {
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

int RsxrDiagram_Bindings() {
    int n = 0;

    AcquireSRWLockShared(&g_bindingLock);

    for (const auto& b : g_bindings) {
        n += b.song != nullptr;
    }

    ReleaseSRWLockShared(&g_bindingLock);

    return n;
}

void RsxrDiagram_Drop(void* song) {
    AcquireSRWLockExclusive(&g_bindingLock);

    for (auto& b : g_bindings) {
        if (b.song == song) {
            RefOp(b.dictionary, 4);
            b = {};
        }
    }

    ReleaseSRWLockExclusive(&g_bindingLock);
}
