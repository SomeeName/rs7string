#include "core/tuner8.h"
#include "core/assetload.h"
#include "core/fncopy.h"
#include "core/fretgrid.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/pitch8.h"
#include "core/string7.h"
#include "core/tuner.h"
#include "core/tuner8_specs.generated.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

extern "C" __declspec(dllexport) uintptr_t g_rs7_native_tuner = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_tu8_labels[8] = {};

void SetLabels(const uint8_t* session);  // the labels of strings 6 / 7 (below)

// ---- the session copies (tools/gen_tuner8s.py) ----
namespace {
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
#include "core/tuner8s.generated.h"
uintptr_t g_sessCopy[kTu8SpecCount]{}, g_sessTramp[kTu8SpecCount]{};

constexpr uintptr_t kTu8CopyBase = 0x40;  // kTu8Sym + 40h + index: the copy of spec `index`

uintptr_t SymAddress(uintptr_t t) {
    if (t - kTu8Sym >= kTu8CopyBase && t - kTu8Sym < kTu8CopyBase + kTu8SpecCount) {
        return g_sessCopy[t - kTu8Sym - kTu8CopyBase];
    }

    switch (t - kTu8Sym) {
        case 0:
            return reinterpret_cast<uintptr_t>(&Rs7Tu8_Advance);
        case 1:
            return reinterpret_cast<uintptr_t>(&Rs7Tu8_OnSelect);
        case 2:
            return reinterpret_cast<uintptr_t>(&Rs7Tu8_First);
        case 3:
            return reinterpret_cast<uintptr_t>(&g_rs7_tu8_labels[0]);
        case 4:
            return reinterpret_cast<uintptr_t>(&Rs7Asset_LetterInline);
        default:
            return 0;
    }
}
}  // namespace

// ---- the GuitarStrings reader copies (TU8H / TU8C / TU8A, tools/gen_tuner8.py) ----
namespace {
uintptr_t g_copy[3]{}, g_tramp[3]{};
// The UI manager has four player slots. Capacity 32 also permits test owners
// and destructor-flag=0 objects without evicting a live registration.
const uint8_t* g_session[32]{};

template <class T>
T Read(const uint8_t* p, uint32_t off) {
    return *reinterpret_cast<const T*>(p + off);
}

template <class T>
T& At(uint8_t* p, uint32_t off) {
    return *reinterpret_cast<T*>(p + off);
}

// One string per extra label ("B", "F#", ...): the precision tuner shows the string's open note.
char g_extraLabel[2][4]{};
uint32_t g_soundString = 0xFFFFFFFF;  // TS8SN1 -> TS8SN2 (game thread, one call)
const uint8_t* g_soundSession = nullptr;
int g_logs = 0;

void Log(const char* fmt, const uint8_t* session, int a = 0, int b = 0, int c = 0) {
    if (g_logs >= 300) {
        return;
    }

    ++g_logs;
    Rs7Log(fmt, session, a, b, c);
}
}  // namespace

extern "C" int __cdecl Rs7Tuner8_HasSession(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    for (const uint8_t* ptr : g_session) {
        if (ptr == session) {
            return 1;
        }
    }

    return 0;
}

extern "C" void __cdecl Rs7Tuner8_InitSession(uint8_t* session) {
    if (!g_rs7_native_tuner || !session) {
        return;
    }

    // Only TU8INIT calls this in the game, at the sole ctor caller's freshly allocated 380h owner.
    // Never probe beyond an unregistered stock owner.
    bool registered = Rs7Tuner8_HasSession(session) != 0;

    if (!registered) {
        for (const uint8_t*& ptr : g_session) {
            if (!ptr) {
                ptr = session;
                registered = true;
                break;
            }
        }
    }

    if (!registered) {
        Rs7Log("tuner8: session registry exhausted - no tail initialized");
        return;
    }

    // The tail is unused (no reader) until TS makes the session native and fills it from the prefix.
    std::memset(session + rs7tu::kStockSize, 0, rs7tu::kSessionSize - rs7tu::kStockSize);

    for (int s = 0; s < 8; ++s) {
        reinterpret_cast<int32_t*>(session + rs7tu::kLastPick)[s] = -2;  // as 5AE0E9
    }

    *reinterpret_cast<uint32_t*>(session + rs7tu::kMagicOff) = rs7tu::kMagic;
}

extern "C" uint32_t __cdecl Rs7Tuner8_FreeSessionSize(uint8_t* session) {
    for (const uint8_t*& ptr : g_session) {
        if (ptr && ptr == session) {
            ptr = nullptr;
            return rs7tu::kSessionSize;
        }
    }

    return rs7tu::kStockSize;
}

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

    return g_copy[kind] && Rs7Tuner8_Use(kind, reg) ? g_copy[kind] : g_tramp[kind];
}

// ---- pure helpers ----
void rs7tu::Order(int n, uint8_t out[8]) {
    for (int i = 0; i < 8; ++i) {
        out[i] = uint8_t(i);
    }

    if (n <= 6) {
        return;
    }

    int k = 0;

    for (int s = n - 1; s >= 6; --s) {
        out[k++] = uint8_t(s);
    }

    for (int s = 0; s < 6; ++s) {
        out[k++] = uint8_t(s);
    }
}

int rs7tu::Step(const uint8_t* order, int n, int cur, int dir) {
    if (n < 1 || n > 8) {
        return -1;
    }

    for (int r = 0; r < n; ++r) {
        if (order[r] == cur) {
            const int to = r + dir;

            return to >= 0 && to < n ? order[to] : -1;
        }
    }

    return -1;
}

void rs7tu::Targets(const int* offs, const int extra[2], int open[8], int off[8]) {
    for (int s = 0; s < 8; ++s) {
        off[s] = s < 6 ? offs[s] : extra[s - 6];
        open[s] = kGuitarBase[s] + off[s];
    }
}

const char* rs7tu::NoteName(int midi) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    return kNames[((midi % 12) + 12) % 12];
}

// ---- native sessions ----
extern "C" int __cdecl Rs7Tuner8_IsNative(const uint8_t* session) {
    if (!g_rs7_native_tuner || !Rs7Tuner8_HasSession(session)) {
        return 0;
    }

    return Read<uint32_t>(session, rs7tu::kMagicOff) == rs7tu::kMagic && Read<uint8_t>(session, rs7tu::kNative) ? 1 : 0;
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
    if (g_sessCopy[index] && !Rs7Tuner8_IsNative(session) && Rs7Tuner8_HasSession(session)) {
        Rs7Tuner8_EnsureNative(const_cast<uint8_t*>(session), kTu8Specs[index].id);
    }
    if (!g_sessCopy[index] || !Rs7Tuner8_IsNative(session)) {
        return g_sessTramp[index];
    }

    if (index == 0) {
        SetLabels(session);  // TS8SEL: the precision tuner's labels of this session's strings
    }

    // C-lite: the guitar already has the extra strings (built with them at song selection); the tuner
    // marks nothing.
    return g_sessCopy[index];
}

namespace {
uint8_t Count(const uint8_t* session) {
    return Read<uint8_t>(session, rs7tu::kCount);
}

// The lowest open MIDI of the session's strings.
int LowestOpen(const uint8_t* session) {
    const int n = Count(session);
    int low = 127;

    for (int s = 0; s < n && s < 8; ++s) {
        low = (std::min)(low, Read<int>(session, rs7tu::kOpen + 4 * s));
    }

    return low;
}

}  // namespace

void SetLabels(const uint8_t* session) {  // game thread (SelectString, TS)
    for (int s = 6; s < 8; ++s) {
        const int midi = Read<int>(session, rs7tu::kOpen + 4 * s);

        std::snprintf(g_extraLabel[s - 6], sizeof g_extraLabel[0], "%s", rs7tu::NoteName(midi));
        g_rs7_tu8_labels[s] = reinterpret_cast<uintptr_t>(g_extraLabel[s - 6]);
    }
}

extern "C" int __stdcall Rs7Tu8_AdvanceC(uint8_t* session, int dir) {
    if (!Rs7Tuner8_IsNative(session)) {
        return 0;
    }

    const int to = rs7tu::Step(session + rs7tu::kOrder, Count(session), Read<int>(session, rs7tu::kCur), dir);
    if (to < 0) {
        return 0;
    }

    At<int>(session, rs7tu::kCur) = to;

    return 1;
}

extern "C" uint32_t __stdcall Rs7Tu8_FirstC(uint8_t* session) {
    return Rs7Tuner8_IsNative(session) ? Read<uint8_t>(session, rs7tu::kOrder) : 0;
}

// stdcall (session, dir) -> EAX; ECX / EDX preserved (the copies keep values there across the step).
extern "C" __declspec(naked) void Rs7Tu8_Advance() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Rs7Tu8_AdvanceC
        pop edx
        pop ecx
        ret 8
    }
}

extern "C" __declspec(naked) void Rs7Tu8_First() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        call Rs7Tu8_FirstC
        pop edx
        pop ecx
        ret 4
    }
}

extern "C" void __cdecl Rs7Tu8_OnSelect(uint8_t* session, uint32_t s) {
    __try {
        if (!Rs7Tuner8_IsNative(session)) {
            return;
        }

        SetLabels(session);
        if (g_rs7_lowrange) {
            Rs7Tuner_LowerDetectorTo(session, LowestOpen(session));
        }
        if (s < 8) {
            Log("tuner8: session %p selects string %d, target MIDI %d", session, int(s),
                Read<int>(session, rs7tu::kOpen + 4 * s));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int Rs7Tuner8_WrongEntries(Rs7WrongEnt* out, int max) {
    int n = 0;

    for (const uint8_t* s : g_session) {
        if (s && n < max && Rs7Tuner8_IsNative(s)) {
            const int low = LowestOpen(s);

            out[n++] = Rs7WrongEnt{const_cast<uint8_t*>(s), low - 1 < 34 ? low - 1 : 34};
        }
    }

    return n;
}

namespace {
// Prefix -> tail: values copied, entity pointers moved (exactly one owner at any time).
void Migrate(uint8_t* session) {
    for (int s = 0; s < 6; ++s) {
        At<int>(session, rs7tu::kOpen + 4 * s) = Read<int>(session, rs7tu::kStockOpen + 4 * s);
        At<int>(session, rs7tu::kOffsets + 4 * s) = Read<int>(session, rs7tu::kStockOff + 4 * s);
        At<float>(session, rs7tu::kPeaks + 4 * s) = Read<float>(session, rs7tu::kStockPeaks + 4 * s);
        At<int>(session, rs7tu::kLastPick + 4 * s) = Read<int>(session, rs7tu::kStockLast + 4 * s);

        for (int k = 0; k < 2; ++k) {
            uint32_t& from = At<uint32_t>(session, rs7tu::kStockLetters + 8 * s + 4 * k);

            At<uint32_t>(session, rs7tu::kLetters + 8 * s + 4 * k) = from;
            from = 0;
        }

        uint32_t& pick = At<uint32_t>(session, rs7tu::kStockPicks + 4 * s);

        At<uint32_t>(session, rs7tu::kPicks + 4 * s) = pick;
        pick = 0;
    }

    for (int s = 6; s < 8; ++s) {
        At<float>(session, rs7tu::kPeaks + 4 * s) = 0.0f;
        At<int>(session, rs7tu::kLastPick + 4 * s) = -2;
        At<uint32_t>(session, rs7tu::kLetters + 8 * s) = 0;
        At<uint32_t>(session, rs7tu::kLetters + 8 * s + 4) = 0;
        At<uint32_t>(session, rs7tu::kPicks + 4 * s) = 0;
    }
}

using ReleaseFn = void(__thiscall*)(void*);

void ReleaseRef(uint32_t& slot) {
    void* p = reinterpret_cast<void*>(uintptr_t(slot));

    slot = 0;
    if (!p) {
        return;
    }

    __try {
        void** vt = *static_cast<void***>(p);

        reinterpret_cast<ReleaseFn>(vt[2])(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

bool Rs7Tuner8_Apply(uint8_t* session, const int* offs, int n, const int extra[2]) {
    if (!g_rs7_native_tuner || !session || !offs) {
        return false;
    }

    __try {
        if (!Rs7Tuner8_HasSession(session) || Read<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            if (n > 6) {
                Log("tuner8: session %p is not a grown 380h session - %d-string target -> stock tuner", session, n);
            }
            return false;
        }

        const bool was = Read<uint8_t>(session, rs7tu::kNative) != 0;
        const unsigned built = Rs7Tuner8_GuitarCount(session);
        // A 6-string target on a stock session runs the game's code - unless the guitar is built with 7/8 strings
        // (fix/menutuner: the stock methods loop to InstrumentInfo +0Bh over six-slot arrays).
        if (!rs7tu::GoesNative(n, was, built)) {
            return false;
        }

        if (n <= 6 && !was) {
            Log("tuner8: session %p - %d-string target on a guitar built with %d strings -> native session "
                "(fix/menutuner)",
                session, n, int(built));
        }

        if (n < 1 || n > 8) {
            return false;
        }

        // The copies read GuitarStrings main[8] (+40h) and its native count: a guitar bound to the session
        // must have a native GuitarStrings (every one allocated with native_strings=1 has).
        if (uint8_t* g = Read<uint8_t*>(session, 0x2C)) {
            if (!rs7gs::IsNative(Read<uint8_t*>(g, 0xC))) {
                if (!was) {
                    Log("tuner8: session %p guitar %08X has no native GuitarStrings - %d-string target -> stock tuner",
                        session, int(reinterpret_cast<uintptr_t>(g)), n);
                }
                if (!was) {
                    return false;
                }
            }
        }

        // TS8PKS / TS8PKP carry the native FretGrid row stride (FGR sites): a session becomes native only after the
        // deferred layout has been written (core/fretgrid.h OnGuitarUpdate); until then it stays on the stock tuner.
        if (!was && rs7fg::RowStride() != rs7fg::kRowNative) {
            Log("tuner8: session %p - FretGrid layout not written yet - %d-string target -> stock tuner", session, n);
            return false;
        }

        if (!was) {
            Migrate(session);
        }
        int open[8], off[8];

        rs7tu::Targets(offs, extra, open, off);
        uint8_t* guitar = Read<uint8_t*>(session, 0x2C);
        uint8_t* info = guitar ? Read<uint8_t*>(guitar, 0x24) : nullptr;

        if (n <= 6 && info && Read<uint8_t>(info, 0xB) == 4) {       // a bass target on a native session
            static const int kBassBase[6] = {28, 33, 38, 43, 0, 0};  // 0x1199CA4

            for (int s = 0; s < 6; ++s) {
                open[s] = kBassBase[s] + off[s];
            }
        }

        for (int s = 0; s < 8; ++s) {
            At<int>(session, rs7tu::kOpen + 4 * s) = open[s];
            At<int>(session, rs7tu::kOffsets + 4 * s) = off[s];
        }

        At<uint8_t>(session, rs7tu::kCount) = uint8_t(n);
        rs7tu::Order(n, session + rs7tu::kOrder);
        At<uint8_t>(session, rs7tu::kNative) = 1;
        SetLabels(session);
        Rs7Tuner_Refresh();
        Log("tuner8: session %p is a native %d-string tuner session, first string %d, lowest open MIDI %d", session, n,
            Read<uint8_t>(session, rs7tu::kOrder), LowestOpen(session));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

unsigned Rs7Tuner8_GuitarCount(const uint8_t* session) {
    if (!session) {
        return 0;
    }

    __try {
        const uint8_t* g = Read<uint8_t*>(session, 0x2C);
        const uint8_t* info = g ? Read<uint8_t*>(g, 0x24) : nullptr;

        return info ? Read<uint8_t>(info, 0xB) : 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool Rs7Tuner8_EnsureNative(uint8_t* session, const char* where) {
    if (!g_rs7_native_tuner || !session) {
        return false;
    }
    if (Rs7Tuner8_IsNative(session)) {
        return true;
    }

    static const uint8_t* s_failed = nullptr;  // game thread: one failure line per session

    __try {
        if (!Rs7Tuner8_HasSession(session) || Read<uint32_t>(session, rs7tu::kMagicOff) != rs7tu::kMagic) {
            return false;
        }

        const unsigned built = Rs7Tuner8_GuitarCount(session);
        if (built <= 6) {
            return false;
        }

        int offs[6];

        for (int s = 0; s < 6; ++s) {
            offs[s] = Read<int>(session, rs7tu::kStockOff + 4 * s);
        }

        const int extra[2] = {0, 0};

        if (Rs7Tuner8_Apply(session, offs, 6, extra)) {
            Rs7Log("tuner8: session %p was a stock session on a guitar built with %u strings - native before %s "
                   "(fix/menutuner)",
                   session, built, where ? where : "?");
            return true;
        }

        if (s_failed != session) {
            s_failed = session;
            Rs7Log(
                "tuner8: session %p on a guitar built with %u strings could NOT be made native before %s - the stock "
                "six-slot tuner runs: REPORT THIS",
                session, built, where ? where : "?");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return false;
}

// ---- mid-hooks ----
// 5AFB19 (mode 0 entry, EBX session): `mov [ebx+74h], esi(0); mov esi, ebx` replayed, then the first
// string of the order.
extern "C" void __cdecl Rs7_Mid_TS8F1(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ebx));

    if (Rs7Tuner8_IsNative(s)) {
        At<int>(s, rs7tu::kCur) = Read<uint8_t>(s, rs7tu::kOrder);
        c.esi = c.ebx;
        c.jump = 0x5AFB1E;
    }
}

// 5AFD15 (capo step 0x5AFCF0, EBX session, curString just set to 0): `mov edi, [ebx+74h]; call
// UI_PlayerTuner_SelectString` replayed after curString = the first string of the order.
extern "C" void __cdecl Rs7_Mid_TS8F2(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ebx));

    if (Rs7Tuner8_IsNative(s)) {
        At<int>(s, rs7tu::kCur) = Read<uint8_t>(s, rs7tu::kOrder);
    }
}

// 5B02A7 (mode 3 entry 0x5B0270, ESI session, EDI = 0, or 5 for the reverse pass): `call
// UI_PlayerTuner_SelectString` replayed with EDI = the first string of the order (the reverse pass keeps
// the stock 5: the last string of every order).
extern "C" void __cdecl Rs7_Mid_TS8F3(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));

    if (Rs7Tuner8_IsNative(s) && !Read<uint8_t>(s, 0x22D)) {
        c.edi = Read<uint8_t>(s, rs7tu::kOrder);
    }
}

// 5AE76D (session reset 0x5AE720 after ResetStandard, ESI session): the tail takes the standard tuning
// and zero peaks too. The replay (`mov byte [esi+196h], bl`) runs.
extern "C" void __cdecl Rs7_Mid_TS8RS(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    for (int k = 0; k < 8; ++k) {
        At<int>(s, rs7tu::kOpen + 4 * k) = k < 6 ? Read<int>(s, rs7tu::kStockOpen + 4 * k) : rs7tu::kGuitarBase[k];
        At<int>(s, rs7tu::kOffsets + 4 * k) = 0;
        At<float>(s, rs7tu::kPeaks + 4 * k) = 0.0f;
    }
}

// 5AE360 (UI_PlayerTuner dtor entry, ECX session): a native session hands its six entities back to the
// prefix (the stock dtor releases them) and releases strings 6 / 7 itself.
extern "C" void __cdecl Rs7_Mid_TS8DT(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ecx));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    __try {
        for (int k = 0; k < 8; ++k) {
            uint32_t& pick = At<uint32_t>(s, rs7tu::kPicks + 4 * k);
            uint32_t& l0 = At<uint32_t>(s, rs7tu::kLetters + 8 * k);
            uint32_t& l1 = At<uint32_t>(s, rs7tu::kLetters + 8 * k + 4);

            if (k < 6) {
                At<uint32_t>(s, rs7tu::kStockPicks + 4 * k) = pick;
                At<uint32_t>(s, rs7tu::kStockLetters + 8 * k) = l0;
                At<uint32_t>(s, rs7tu::kStockLetters + 8 * k + 4) = l1;
                pick = l0 = l1 = 0;
            } else {
                ReleaseRef(pick);
                ReleaseRef(l1);
                ReleaseRef(l0);
            }
        }

        At<uint8_t>(s, rs7tu::kNative) = 0;
        Log("tuner8: session %p destroyed (native tail released)", s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    Rs7Tuner_Refresh();
}

// 5B2D88 (UI_PlayerTuner_CheckWrongInstrument, ESI session): `mov ecx, [ebp+8]; mov edx, [esi+ecx*4+38h]`.
extern "C" void __cdecl Rs7_Mid_TS8WR(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    const uint32_t str = *reinterpret_cast<const uint32_t*>(uintptr_t(c.ebp) + 8);

    c.ecx = str;
    c.edx = str < 8 ? Read<uint32_t>(s, rs7tu::kOpen + 4 * str) : 0;
    c.jump = 0x5B2D8F;
}

// 5B2E10 (UI_PlayerTuner_PlayTunedSound entry, ECX string, [esp+4] session): the string before the
// stock clamp to the six Wwise colours.
extern "C" void __cdecl Rs7_Mid_TS8SN1(Rs7Mid::Context& c) {
    g_soundString = c.ecx;
    g_soundSession = *reinterpret_cast<const uint8_t* const*>(uintptr_t(c.esp) + 4);
}

// 5B2EA7: `mov eax, [ebp-24h]; mov eax, [eax+edi*4+38h]` - the note name of the string itself (EDI is
// clamped to 5 for the colour name).
extern "C" void __cdecl Rs7_Mid_TS8SN2(Rs7Mid::Context& c) {
    uint8_t* s = *reinterpret_cast<uint8_t* const*>(uintptr_t(c.ebp) - 0x24);
    const uint32_t str = g_soundString;

    g_soundString = 0xFFFFFFFF;
    if (!Rs7Tuner8_IsNative(s) || s != g_soundSession || str >= 8) {
        return;
    }

    c.eax = Read<uint32_t>(s, rs7tu::kOpen + 4 * str);
    c.jump = 0x5B2EAE;
}

// 5B63E0 / 5B66AD (Play_Tuning_ApplyToDetection entry / its one exit): the profile's six strings go to
// the detector and matcher in between (core/pitch8.h g_rs7_profile_apply).
extern "C" void __cdecl Rs7_Mid_TS8PA(Rs7Mid::Context&) {
    g_rs7_profile_apply = 1;
}

extern "C" void __cdecl Rs7_Mid_TS8PZ(Rs7Mid::Context&) {
    g_rs7_profile_apply = 0;
}

int Rs7Pitch_KeepSongTuning() {
    return g_rs7_native_tuner ? 1 : 0;
}

// ---- startup ----
bool Rs7Tuner8_BuildCopies() {
    for (int i = 0; i < kRs7FnSpecCount; ++i) {
        const auto& sp = kRs7FnSpecs[i];
        char why[160]{};

        if (!Rs7Fn_Build(sp, sp.va, &g_copy[i], &g_tramp[i], why, sizeof why)) {
            Rs7Log("tuner8: %s - native_tuner off", why);

            for (int k = 0; k < 3; ++k) {
                g_copy[k] = g_tramp[k] = 0;
            }

            return false;
        }
    }

    // Pass 1: every copy (the copies call each other), pass 2: relocations and trampolines.
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
        g_sessCopy[k] = reinterpret_cast<uintptr_t>(p);
    }

    for (int k = 0; k < kTu8SpecCount; ++k) {
        const Tu8Spec& s = kTu8Specs[k];
        auto* p = reinterpret_cast<unsigned char*>(g_sessCopy[k]);

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

        if (s.entry) {  // a copy with an entry site: its trampoline replays the stock entry bytes
            unsigned char* t = p + s.len;

            std::memcpy(t, reinterpret_cast<const void*>(s.va), s.entry);
            t[s.entry] = 0xE9;
            const int32_t back = int32_t(s.va + s.entry - reinterpret_cast<uintptr_t>(t + s.entry + 5));

            std::memcpy(t + s.entry + 1, &back, 4);
            g_sessTramp[k] = reinterpret_cast<uintptr_t>(t);
        }

        FlushInstructionCache(GetCurrentProcess(), p, s.len + s.entry + 5);
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
