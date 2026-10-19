#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8_specs.generated.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

// The native tuner session's code copies: three GuitarStrings readers (TU8H / TU8C / TU8A, tools/gen_tuner8.py) and
// the UI_PlayerTuner methods (TS8*, tools/gen_tuner8s.py), built from the live code at startup, and the entry
// dispatch that picks the copy or the stock code (trampoline) per call.

namespace {
using rs7tuner::g_state;
using rs7tuner::Read;

// ---- the session copies (tools/gen_tuner8s.py) ----
struct Tu8Rel {
    unsigned off;
    uintptr_t target;
    unsigned abs;
};

struct Tu8Spec {
    const char* id;
    uintptr_t va;
    unsigned stockLen, hash, entry, sess;
    const unsigned char* code;
    unsigned len;
    const Tu8Rel* rels;
    unsigned nrels;
};

// Synthetic targets of the generated code, in the generator's SYMS order.
constexpr uintptr_t kTu8Sym = 0x70000000;
#include "features/tuner/tuner8s.generated.h"
static_assert(kTu8SpecCount == rs7tuner::kSessionCopies, "state.h sizes the copy tables from tuner8s_ids");

constexpr uintptr_t kTu8CopyBase = 0x40;  // kTu8Sym + 40h + index: the copy of spec `index`

uintptr_t SymAddress(uintptr_t t) {
    if (t - kTu8Sym >= kTu8CopyBase && t - kTu8Sym < kTu8CopyBase + kTu8SpecCount) {
        return g_state.sessCopy[t - kTu8Sym - kTu8CopyBase];
    }

    switch (t - kTu8Sym) {
        case 0:
            return g_rs7_callin_Tu8Advance.Entry();
        case 1:
            return reinterpret_cast<uintptr_t>(&Rs7Tu8_OnSelect);
        case 2:
            return g_rs7_callin_Tu8First.Entry();
        case 3:
            return reinterpret_cast<uintptr_t>(&g_rs7_tu8_labels[0]);
        case 4:
            return reinterpret_cast<uintptr_t>(&Rs7Asset_LetterInline);
        default:
            return 0;
    }
}
}  // namespace

// ---- entry dispatch ----
extern "C" int __cdecl Rs7Tuner8_Use(uint32_t kind, uint8_t* reg) {
    if (!g_rs7_native_tuner || !g_rs7_native_strings || !g_rs7_native_fretgrid || kind >= 3 || !reg) {
        return 0;
    }

    __try {
        uint8_t* guitar = Read<uint8_t*>(reg, kind == 2 ? 0x37C : 0x2C);
        if (!guitar) {
            return 0;
        }

        uint8_t* gsObj = Read<uint8_t*>(guitar, 0xC);
        if (!rs7gs::IsNative(gsObj)) {
            return 0;
        }

        uint8_t* info = Read<uint8_t*>(guitar, 0x24);
        uint8_t* owner = Read<uint8_t*>(gsObj, 0);
        if (!info || !owner || Read<uint8_t*>(owner, 0x24) != info || Read<uint8_t*>(gsObj, rs7gs::kInfo) != info ||
            Read<uint8_t*>(gsObj, rs7gs::kGuitar) != guitar ||
            Read<uint32_t>(info, rs7gs::kInfoMagicOff) != rs7gs::kInfoMagic) {
            return 0;
        }

        const unsigned active = info[rs7gs::kInfoActive], loaded = info[rs7gs::kInfoLoaded];
        if (!active || active > loaded || loaded > rs7gs::kCapacity) {
            return 0;
        }

        for (unsigned s = 0; s < active; ++s) {
            if (!rs7gs::Child(gsObj, int(s), false)) {
                return 0;
            }
        }

        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

extern "C" uintptr_t __cdecl Rs7Tuner8_Target(uint32_t kind, uint8_t* reg) {
    if (kind >= 3) {
        return 0;  // entry stubs only pass constants 0..2
    }

    return g_state.readerCopy[kind] && Rs7Tuner8_Use(kind, reg) ? g_state.readerCopy[kind] : g_state.readerTramp[kind];
}

extern "C" uintptr_t __cdecl Rs7Tuner8_SessTarget(uint32_t index, const uint32_t* regs) {
    if (index >= uint32_t(kTu8SpecCount)) {
        return 0;
    }

    const Tu8Spec& sp = kTu8Specs[index];
    uint32_t s = 0;

    switch (sp.sess) {
        case 0:
            s = regs[7];
            break;  // EAX
        case 1:
            s = regs[6];
            break;  // ECX
        case 2:
            s = regs[5];
            break;  // EDX
        case 3:
            s = regs[4];
            break;  // EBX
        case 6:
            s = regs[1];
            break;  // ESI
        case 7:
            s = regs[0];
            break;  // EDI
        case 8:
            s = regs[10];
            break;  // [esp+4] at the entry
        case 9:
            s = regs[5] - 0xD0;
            break;  // EDX = the session's embedded NoteTuner
        default:
            break;
    }

    const uint8_t* session = reinterpret_cast<const uint8_t*>(uintptr_t(s));

    // fix/menutuner (a): a stock session on a guitar built with 7/8 strings (a 6-string target, or one TS never made
    // native) runs the native copy too; the stock method would index its six-slot arrays with string 6.
    if (g_state.sessCopy[index] && !Rs7Tuner8_IsNative(session) && Rs7Tuner8_HasSession(session)) {
        Rs7Tuner8_EnsureNative(const_cast<uint8_t*>(session), kTu8Specs[index].id);
    }
    if (!g_state.sessCopy[index] || !Rs7Tuner8_IsNative(session)) {
        return g_state.sessTramp[index];
    }

    if (index == 0) {
        rs7tuner::SetLabels(session);  // TS8SEL: the precision tuner's labels of this session's strings
    }

    // C-lite: the guitar already has the extra strings (built with them at song selection); the tuner
    // marks nothing.
    return g_state.sessCopy[index];
}

// ---- startup ----
namespace {
// Copies the code of every session spec (pass 1: the copies call each other, so all exist before any is relocated).
bool AllocSessionCopies() {
    for (int k = 0; k < kTu8SpecCount; ++k) {
        const Tu8Spec& s = kTu8Specs[k];

        if (Rs7Fn_Fnv(reinterpret_cast<const uint8_t*>(s.va), s.stockLen) != s.hash) {
            Rs7Log("tuner8: %s bytes at %08X differ from the generated copy - native_tuner off", s.id, unsigned(s.va));
            return false;
        }

        auto* p = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, s.len + s.entry + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!p) {
            return false;
        }

        std::memcpy(p, s.code, s.len);
        g_state.sessCopy[k] = reinterpret_cast<uintptr_t>(p);
    }

    return true;
}

// Pass 2: the relocations of copy k and, for a copy with an entry site, its trampoline (the stock entry bytes + jmp
// back).
bool LinkSessionCopy(int k) {
    const Tu8Spec& s = kTu8Specs[k];
    auto* p = reinterpret_cast<unsigned char*>(g_state.sessCopy[k]);

    for (unsigned r = 0; r < s.nrels; ++r) {
        uintptr_t t = s.rels[r].target;

        if (t >= kTu8Sym && t < kTu8Sym + 0x100) {
            t = SymAddress(t);
        }
        if (!t) {
            return false;
        }

        if (s.rels[r].abs) {
            const uint32_t a = uint32_t(t);

            std::memcpy(p + s.rels[r].off, &a, 4);
        } else {
            const int32_t rel = int32_t(t - reinterpret_cast<uintptr_t>(p + s.rels[r].off + 4));

            std::memcpy(p + s.rels[r].off, &rel, 4);
        }
    }

    if (s.entry) {
        unsigned char* t = p + s.len;

        std::memcpy(t, reinterpret_cast<const void*>(s.va), s.entry);
        t[s.entry] = 0xE9;
        const int32_t back = int32_t(s.va + s.entry - reinterpret_cast<uintptr_t>(t + s.entry + 5));

        std::memcpy(t + s.entry + 1, &back, 4);
        g_state.sessTramp[k] = reinterpret_cast<uintptr_t>(t);
    }

    FlushInstructionCache(GetCurrentProcess(), p, s.len + s.entry + 5);

    return true;
}
}  // namespace

bool Rs7Tuner8_BuildCopies() {
    for (int i = 0; i < kRs7FnSpecCount; ++i) {
        const auto& sp = kRs7FnSpecs[i];
        char why[160]{};

        if (!Rs7Fn_Build(sp, sp.va, &g_state.readerCopy[i], &g_state.readerTramp[i], why, sizeof why)) {
            Rs7Log("tuner8: %s - native_tuner off", why);

            for (int k = 0; k < 3; ++k) {
                g_state.readerCopy[k] = g_state.readerTramp[k] = 0;
            }

            return false;
        }
    }

    if (!AllocSessionCopies()) {
        return false;
    }

    for (int k = 0; k < kTu8SpecCount; ++k) {
        if (!LinkSessionCopy(k)) {
            return false;
        }
    }

    // Labels 0..5: the stock table 0x12E8644 (read, never written).
    __try {
        const uintptr_t* stock = reinterpret_cast<const uintptr_t*>(0x12E8644);

        for (int s = 0; s < 6; ++s) {
            g_rs7_tu8_labels[s] = stock[s];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    Rs7Log("tuner8: %d GuitarStrings reader copies and %d session method copies built", kRs7FnSpecCount, kTu8SpecCount);

    return true;
}

void Rs7Tuner8_Prepare() {
    if (g_rs7_native_tuner && (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours)) {
        Rs7Log("tuner8: native_tuner OFF - needs the native strings, FretGrid and colours (now %d %d %d)",
               int(g_rs7_native_strings), int(g_rs7_native_fretgrid), int(g_rs7_native_colours));
        g_rs7_native_tuner = 0;
    }

    if (g_rs7_native_tuner && !Rs7Tuner8_BuildCopies()) {
        g_rs7_native_tuner = 0;
    }
}

void Rs7Tuner8_ApplySkip(const std::string& skip) {
    if (g_rs7_native_tuner && (skip.find(",TU8") != std::string::npos || skip.find(",TS8") != std::string::npos)) {
        g_rs7_native_tuner = 0;
        Rs7Log("tuner8: a native tuner site was skipped - native_tuner off");
    }

    if (g_rs7_native_tuner) {
        Rs7Log("tuner8: native_tuner ON - native 8-slot tuner sessions");
    }
}
