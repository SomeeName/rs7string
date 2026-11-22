// Game JSON operations and the stock full-object apply bridge. No registration grid writes.
#include "features/tones/game.h"
#include "features/tones/toneplan.h"
#include "model/gamestring.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstring>

extern "C" uintptr_t g_rsxr_tone_apply, g_rsxr_tone_manager, g_rsxr_tone_context;

namespace rsxrtones {
namespace {
using Unary = uint32_t(__thiscall*)(void*);
using Find = uint32_t(__thiscall*)(void*, const char*);

struct Bridge {
    uint32_t definition = 0, esi = 0, ret = 0;
};

thread_local Bridge g_bridge;

void ApplyDone(RsxrMid::Context& c) {
    c.esi = g_bridge.esi;
    c.jump = g_bridge.ret;
}

RsxrMid::CallIn g_applyDone{&ApplyDone, RsxrMid::Mode::Full};

uint32_t Virtual(uint32_t object, size_t offset) {
    const uint32_t address = Read(Read(object), offset);
    if (!object || !address) {
        return 0;
    }

    __try {
        return reinterpret_cast<Unary>(address)(reinterpret_cast<void*>(object));
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

uint32_t Member(uint32_t object, const char* key, size_t offset = 0x14) {
    const uint32_t address = Read(Read(object), offset);
    if (!object || !address) {
        return 0;
    }

    __try {
        return reinterpret_cast<Find>(address)(reinterpret_cast<void*>(object), key);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

// CONFIRMED: 0x7D3D64 reads [ESI]; ret 0Ch. Stack = manager, player, activation kind (stock uses 3).
void ApplyEntry(RsxrMid::Context& c) {
    // The game's custom ABI takes ESI as input and leaves it untouched. Restore the C++ caller's ESI after ret 0Ch.
    g_bridge.esi = c.esi;
    g_bridge.ret = RsxrMid::Dword(c.esp);
    const uint32_t done = static_cast<uint32_t>(g_applyDone.Entry());

    if (!Rsxr_SafeStore(reinterpret_cast<void*>(uintptr_t(c.esp)), &done, sizeof done)) {
        c.eax = 0;
        RsxrMid::Ret(c, 12);
        return;
    }

    c.esi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_bridge.definition));
    c.jump = static_cast<uint32_t>(g_rsxr_tone_apply);
}

RsxrMid::CallIn g_apply{&ApplyEntry, RsxrMid::Mode::Full, 12};
}  // namespace

uint32_t Read(uint32_t base, size_t offset) {
    return Rsxr_SafeRead<uint32_t>(reinterpret_cast<const void*>(uintptr_t(base)), offset);
}

uint32_t Attributes(uint32_t arrangement) {
    // CONFIRMED: 0x889CA0 resolves an object member through virtual +18h, then +5Ch.
    return Virtual(Member(arrangement, "Attributes", 0x18), 0x5C);
}

bool Name(uint32_t attributes, const char* key, char* text, size_t size) {
    // CONFIRMED: manifest reader 0x8899F0 uses virtual +60h, then value+8 -> game string.
    // +64h belongs to the automatic consumer's different JSON access path (0x465119).
    const uint32_t value = Virtual(Member(attributes, key), 0x60);

    return value && rsxr::ReadGameString(reinterpret_cast<const void*>(uintptr_t(Read(value, 8))), text, size);
}

bool Reference(uint32_t object, bool retain) {
    const uint32_t address = Read(Read(object), retain ? 0 : 4);
    if (!object || !address) {
        return false;
    }

    __try {
        reinterpret_cast<void(__thiscall*)(void*)>(address)(reinterpret_cast<void*>(object));
        return true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

uint32_t Definition(uint32_t collection, const char* key) {
    // CONFIRMED: 0x7D3790 scans the JSON collection at +10h/+14h, unwraps each value through +34h,
    // compares Key case-insensitively, then returns the canonical object through virtual +5Ch.
    const uint32_t begin = Read(collection, 0x10), end = Read(collection, 0x14);
    if (!begin || end < begin || (end - begin) % 4 || (end - begin) / 4 > 4096) {
        return 0;
    }

    uint32_t result = 0;
    bool matched = false;

    for (uint32_t at = begin; at < end; at += 4) {
        uint32_t node = Read(at);
        const uint32_t wrapped = Virtual(node, 0x34);

        if (wrapped) {
            node = Read(Read(wrapped, 0xC), 8);

            if (!node) {
                node = Read(wrapped, 0x10);
            }
        }

        char name[256]{};

        if (!Name(node, "Key", name, sizeof name) || _stricmp(name, key)) {
            continue;
        }

        if (matched) {
            return 0;  // ambiguous Key: refuse rather than choose a definition by collection order
        }

        matched = true;
        result = Virtual(node, 0x5C);
    }

    return result && Reference(result, true) ? result : 0;
}

bool Apply(uint32_t object, uint32_t player) {
    const uintptr_t entry = g_apply.Entry();
    const uint32_t manager = Read(static_cast<uint32_t>(g_rsxr_tone_manager));
    if (!entry || !g_applyDone.Entry() || !g_rsxr_tone_apply || !object || !manager || player >= 2) {
        return false;
    }

    const Bridge previous = g_bridge;
    bool applied = false;

    g_bridge.definition = object;

    __try {
        applied = reinterpret_cast<uint8_t(__stdcall*)(uint32_t, uint32_t, uint32_t)>(entry)(manager, player, 3) != 0;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        applied = false;
    }

    g_bridge = previous;

    return applied;
}

uint32_t Override(uint32_t player, uint32_t slot) {
    const char* extension = SlotExtension(slot);
    if (player >= 2 || !extension) {
        return 0;
    }

    // CONFIRMED: 0x41B310 resolves CustomTone/<decimal player> from manager+14h's context at +20h.
    // Stock playback 0x464BDD and registration 0x7E9B83 then resolve the slot member through +18h/+5Ch.
    uint32_t context = Read(Read(Read(static_cast<uint32_t>(g_rsxr_tone_context)), 0x14), 0x20);

    context = Virtual(Member(context, "CustomTone", 0x18), 0x5C);
    const char playerKey[2] = {static_cast<char>('0' + player), 0};

    context = Virtual(Member(context, playerKey, 0x18), 0x5C);
    char property[64]{};

    strcpy_s(property, "LAS_ToneCustomData_");
    strcat_s(property, extension);
    const uint32_t object = Virtual(Member(context, property, 0x18), 0x5C);

    return object && Reference(object, true) ? object : 0;
}
}  // namespace rsxrtones
