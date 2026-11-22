#include "features/colours/colours.h"
#include "features/fretgrid/fretgrid.h"
#include "features/headstock/headstock.h"
#include "features/strings/strings.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include "features/tuner/tuner8_specs.generated.h"
#include "model/instrument.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstring>

// The native tuner session's code copies: three GuitarStrings readers (TU8H / TU8C / TU8A, tools/gen_tuner8.py) and
// the UI_PlayerTuner methods (TS8*, tools/gen_tuner8s.py), built from the live code at startup, and the entry
// dispatch that picks the copy or the stock code (trampoline) per call.

namespace {
using rsxrtuner::g_state;
using rsxrtuner::UncheckedRead;

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
static_assert(kTu8SpecCount == rsxrtuner::kSessionCopies, "state.h sizes the copy tables from tuner8s_ids");

constexpr uintptr_t kTu8CopyBase = 0x40;  // kTu8Sym + 40h + index: the copy of spec `index`

uintptr_t SymAddress(uintptr_t t) {
    if (t - kTu8Sym >= kTu8CopyBase && t - kTu8Sym < kTu8CopyBase + kTu8SpecCount) {
        return g_state.sessCopy[t - kTu8Sym - kTu8CopyBase];
    }

    switch (t - kTu8Sym) {
        case 0:
            return g_rsxr_callin_Tu8Advance.Entry();
        case 1:
            return reinterpret_cast<uintptr_t>(&RsxrTu8_OnSelect);
        case 2:
            return g_rsxr_callin_Tu8First.Entry();
        case 3:
            return reinterpret_cast<uintptr_t>(&g_rsxr_tu8_labels[0]);
        case 4:
            return reinterpret_cast<uintptr_t>(&RsxrAsset_LetterInline);
        default:
            return 0;
    }
}
}  // namespace

// ---- entry dispatch ----
extern "C" int __cdecl RsxrTuner8_Use(uint32_t kind, uint8_t* reg) {
    // Installed readers follow the owned GuitarStrings layout even after runtime isolation.
    if (kind >= 3 || !reg) {
        return 0;
    }

    __try {
        uint8_t* guitar = UncheckedRead<uint8_t*>(reg, kind == 2 ? rsxr::kSessionGuitar : rsxrtu::kGuitar);
        if (!guitar) {
            return 0;
        }

        uint8_t* gsObj = UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarStrings);
        if (!rsxrgs::IsNative(gsObj)) {
            return 0;
        }

        uint8_t* info = UncheckedRead<uint8_t*>(guitar, rsxr::kGuitarInfo);
        uint8_t* owner = UncheckedRead<uint8_t*>(gsObj, 0);
        if (!info || !owner || UncheckedRead<uint8_t*>(owner, rsxr::kGuitarInfo) != info ||
            UncheckedRead<uint8_t*>(gsObj, rsxrgs::kInfo) != info ||
            UncheckedRead<uint8_t*>(gsObj, rsxrgs::kGuitar) != guitar ||
            UncheckedRead<uint32_t>(info, rsxrgs::kInfoMagicOff) != rsxrgs::kInfoMagic) {
            return 0;
        }

        const unsigned active = info[rsxrgs::kInfoActive], loaded = info[rsxrgs::kInfoLoaded];
        if (!active || active > loaded || loaded > rsxrgs::kCapacity) {
            return 0;
        }

        for (unsigned s = 0; s < active; ++s) {
            if (!rsxrgs::Child(gsObj, int(s), false)) {
                return 0;
            }
        }

        return 1;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return 0;
    }
}

extern "C" uintptr_t __cdecl RsxrTuner8_Target(uint32_t kind, uint8_t* reg) {
    if (kind >= 3) {
        return 0;  // entry stubs only pass constants 0..2
    }

    return g_state.readerCopy[kind] && RsxrTuner8_Use(kind, reg) ? g_state.readerCopy[kind] : g_state.readerTramp[kind];
}

extern "C" uintptr_t __cdecl RsxrTuner8_SessTarget(uint32_t index, const uint32_t* regs) {
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

    // A stock session on a guitar built with 7/8 strings (a 6-string target, or one TS never made
    // native) runs the native copy too; the stock method would index its six-slot arrays with string 6.
    if (g_state.sessCopy[index] && !RsxrTuner8_IsNative(session) && RsxrTuner8_HasSession(session)) {
        RsxrTuner8_EnsureNative(const_cast<uint8_t*>(session), kTu8Specs[index].id);
    }
    if (!g_state.sessCopy[index] || !RsxrTuner8_IsNative(session)) {
        return g_state.sessTramp[index];
    }

    if (index == 0) {
        rsxrtuner::SetLabels(session);  // TS8SEL: the precision tuner's labels of this session's strings
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

        if (RsxrFn_Fnv(reinterpret_cast<const uint8_t*>(s.va), s.stockLen) != s.hash) {
            RsxrLog("tuner8: %s bytes at %08X differ from the generated copy - native_tuner off", s.id, unsigned(s.va));
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

// A failed build: frees every session copy (each trampoline lives in its copy's block) and zeroes the pointers, so
// no half-built copy stays reachable.
void ReleaseSessionCopies() {
    for (int k = 0; k < kTu8SpecCount; ++k) {
        if (g_state.sessCopy[k]) {
            VirtualFree(reinterpret_cast<void*>(g_state.sessCopy[k]), 0, MEM_RELEASE);
        }

        g_state.sessCopy[k] = g_state.sessTramp[k] = 0;
    }
}
}  // namespace

bool RsxrTuner8_BuildCopies() {
    for (int i = 0; i < kRsxrFnSpecCount; ++i) {
        const auto& sp = kRsxrFnSpecs[i];
        char why[160]{};

        if (!RsxrFn_Build(sp, sp.va, &g_state.readerCopy[i], &g_state.readerTramp[i], why, sizeof why)) {
            RsxrLog("tuner8: %s - native_tuner off", why);

            for (int k = 0; k < 3; ++k) {
                g_state.readerCopy[k] = g_state.readerTramp[k] = 0;
            }

            return false;
        }
    }

    if (!AllocSessionCopies()) {
        ReleaseSessionCopies();
        return false;
    }

    for (int k = 0; k < kTu8SpecCount; ++k) {
        if (!LinkSessionCopy(k)) {
            ReleaseSessionCopies();
            return false;
        }
    }

    // Labels 0..5: the stock table 0x12E8644 (read, never written).
    __try {
        const uintptr_t* stock = reinterpret_cast<const uintptr_t*>(0x12E8644);

        for (int s = 0; s < 6; ++s) {
            g_rsxr_tu8_labels[s] = stock[s];
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        ReleaseSessionCopies();
        return false;
    }

    RsxrLog("tuner8: %d GuitarStrings reader copies and %d session method copies built", kRsxrFnSpecCount,
            kTu8SpecCount);

    return true;
}

void RsxrTuner8_Prepare() {
    if (g_rsxr_native_tuner && (!g_rsxr_native_strings || !g_rsxr_native_fretgrid || !g_rsxr_native_colours)) {
        RsxrLog("tuner8: native_tuner OFF - needs the native strings, FretGrid and colours (now %d %d %d)",
                int(g_rsxr_native_strings), int(g_rsxr_native_fretgrid), int(g_rsxr_native_colours));
        g_rsxr_native_tuner = 0;
    }

    if (g_rsxr_native_tuner && !RsxrTuner8_BuildCopies()) {
        g_rsxr_native_tuner = 0;
    }
}

void RsxrTuner8_ApplySkip(const std::string& skip) {
    if (g_rsxr_native_tuner && (skip.find(",TU8") != std::string::npos || skip.find(",TS8") != std::string::npos)) {
        g_rsxr_native_tuner = 0;
        RsxrLog("tuner8: a native tuner site was skipped - native_tuner off");
    }

    if (g_rsxr_native_tuner) {
        RsxrLog("tuner8: native_tuner ON - native 8-slot tuner sessions");
    }
}
