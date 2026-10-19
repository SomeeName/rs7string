#include "features/scene/scene.h"      // rs7s7d::SafeCopy
#include "features/string7/string7.h"  // g_rs7_frame_global (X_DT): *0x135FBFC, the localisation context
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuningname.h"
#include "platform/log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>

// Seven-string tuning names, the game side (TJ TU TUT TN, tuningname.h): reads the arrangement's Tuning, asks the
// name logic (tuningname.cpp) and writes the game's strings through its own string routines (callref sites).

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_tn_on = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strcon = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strasg = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_loc = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strdtor = 0;
}

namespace {
using rs7tuner::g_state;
using rs7tuner::JsonNumber;
using rs7tuner::JsonOffset;
using rs7tuner::NameCall;
constexpr long kMaxLogs = 60;

// The song list is rebuilt often: each distinct line is logged once.
__declspec(noinline) void Log(const char* fmt, ...) {
    try {
        char buf[512];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        uint32_t h = 2166136261u;

        for (const char* p = buf; *p; ++p) {
            h = (h ^ uint8_t(*p)) * 16777619u;
        }

        for (uint32_t x : g_state.nameSeen) {
            if (x == h) {
                return;
            }
        }

        g_state.nameSeen[_InterlockedIncrement(&g_state.nameSeenNext) % 64] = h;
        if (_InterlockedIncrement(&g_state.nameLogs) > kMaxLogs) {
            return;
        }

        Rs7Log("%s", buf);
    } catch (...) {
    }
}

// A Tuning object with Rs7String6 and all of string0..5: t[0..6]. Anything else: false (stock).
bool ReadSeven(void* tuning, int t[7]) {
    if (!JsonOffset(tuning, rs7tn::kKey, &t[6])) {
        return false;
    }

    char key[16];

    for (int s = 0; s < 6; ++s) {
        std::snprintf(key, sizeof key, "string%d", s);
        if (!JsonOffset(tuning, key, &t[s])) {
            return false;
        }
    }

    return true;
}

// The game's 0x18-byte string: inline 16 bytes at +0 while +14h == &+10h, else the data pointer
// at +0; +10h = end.
std::string StrRead(const void* s) {
    if (!s) {
        return {};
    }

    const uint8_t* b = static_cast<const uint8_t*>(s);
    const char* end = nullptr;
    const uint8_t* tag = nullptr;
    const char* heap = nullptr;
    if (!rs7s7d::SafeCopy(&end, b + 0x10, 4) || !rs7s7d::SafeCopy(&tag, b + 0x14, 4)) {
        return {};
    }

    const char* begin = reinterpret_cast<const char*>(b);

    if (tag != b + 0x10) {
        if (!rs7s7d::SafeCopy(&heap, b, 4) || !heap) {
            return {};
        }

        begin = heap;
    }

    if (end < begin || end - begin > 1024) {
        return {};
    }

    std::string out(size_t(end - begin), '\0');
    if (!out.empty() && !rs7s7d::SafeCopy(&out[0], begin, out.size())) {
        return {};
    }

    return out;
}

// The game's string routines (callref sites). Custom register ABIs: every register saved around them.
bool CallAssign(void* s, const char* b, const char* e) {
    const uintptr_t fn = g_rs7_fn_strasg;
    if (!fn || !s) {
        return false;
    }

    __try {
        __asm {
            pushad
            push e
            push b
            mov eax, s
            call fn
            popad
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallConstruct(void* s, const char* b, const char* e) {
    const uintptr_t fn = g_rs7_fn_strcon;
    if (!fn || !s) {
        return false;
    }

    __try {
        uint8_t* d = static_cast<uint8_t*>(s);

        *reinterpret_cast<uint8_t**>(d + 0x10) = d;  // empty inline string, as 0x56C31B..
        *reinterpret_cast<uint8_t**>(d + 0x14) = d + 0x10;
        d[0] = 0;

        __asm {
            pushad
            push e
            push b
            mov eax, s
            call fn
            popad
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallLocalise(void* dst, const char* text) {
    const uintptr_t fn = g_rs7_fn_loc;
    uint8_t* app = g_rs7_frame_global ? *reinterpret_cast<uint8_t**>(g_rs7_frame_global) : nullptr;
    if (!fn || !app || !dst) {
        return false;
    }

    __try {
        __asm {
            pushad
            push text
            push 1
            push app
            mov ecx, dst
            call fn
            add esp, 0Ch
            popad
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CallDestroy(void* s) {
    const uintptr_t fn = g_rs7_fn_strdtor;
    if (!fn || !s) {
        return;
    }

    __try {
        __asm {
            pushad
            mov ecx, s
            call fn
            popad
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool Assign(void* s, const std::string& v) {
    return CallAssign(s, v.data(), v.data() + v.size());
}

// "$[17874]E Standard" -> "E STANDARD" through the game's localisation; plain text unchanged.
std::string Localise(const std::string& token) {
    if (token.empty() || token[0] != '$') {
        return token;
    }

    alignas(8) uint8_t buf[0x18] = {};
    if (!CallLocalise(buf, token.c_str())) {
        return token;
    }

    std::string out = StrRead(buf);

    CallDestroy(buf);

    return out.empty() ? token : out;
}

std::string Offsets(const int t[7]) {
    char b[96];

    std::snprintf(b, sizeof b, "%d %d %d %d %d %d | 7th %d", t[0], t[1], t[2], t[3], t[4], t[5], t[6]);

    return b;
}

// Open calls (TJ / TU, state.h NameCall): Pre fills one, Post consumes it.
uint32_t Open(void* raw, void* disp, int cents) {
    for (int i = 0; i < rs7tuner::kNameCalls; ++i) {
        if (_InterlockedCompareExchange(&g_state.calls[i].used, 1, 0) == 0) {
            NameCall& c = g_state.calls[i];

            c.thread = GetCurrentThreadId();
            c.raw = raw;
            c.disp = disp;
            c.cents = cents;
            c.seven = false;
            c.eight = false;
            c.t8 = 0;
            std::memset(c.t, 0, sizeof c.t);
            return uint32_t(i + 1);
        }
    }

    return 0;  // all busy: stock
}

NameCall* Get(uint32_t h) {
    return (h >= 1 && h <= rs7tuner::kNameCalls && g_state.calls[h - 1].used) ? &g_state.calls[h - 1] : nullptr;
}

void Close(NameCall* c) {
    if (c) {
        _InterlockedExchange(&c->used, 0);
    }
}
}  // namespace

void Rs7Tn_Configure(const rs7tn::Config& c) {
    g_state.names = c;
}

// ---- TJ: 0x56C950 ----
extern "C" uint32_t __cdecl Rs7_TnJsonPre(void* raw, void* disp, void* json, int cents) {
    if (!g_rs7_tn_on || !json) {
        return 0;
    }

    int t[7];
    if (!ReadSeven(json, t)) {
        return 0;
    }

    const uint32_t h = Open(raw, disp, cents);

    if (NameCall* c = Get(h)) {
        c->seven = true;
        c->eight = JsonOffset(json, rs7tn::kKey8, &c->t8);  // an 8-string chart
        std::memcpy(c->t, t, sizeof t);
    }

    return h;
}

extern "C" void __cdecl Rs7_TnJsonPost(uint32_t h) {
    NameCall* c = Get(h);
    if (!c) {
        return;
    }

    try {
        const std::string raw0 = StrRead(c->raw), disp0 = StrRead(c->disp);
        const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
        const char* n = c->eight ? rs7tn::Name8(t8) : rs7tn::Name7(c->t);
        const std::string suffix8 = rs7tn::Suffix8(g_state.names.suffix);
        const char* suffix = c->eight ? suffix8.c_str() : g_state.names.suffix;
        std::string raw = raw0, disp;

        if (n) {
            // Raw token (row +150h, the tuning sort's group equality) and the display text: the
            // 7-string name; the display decorated like a matched stock name.
            raw = rs7tn::Display(rs7tn::Compose(n, g_state.names.prefix, suffix));
            disp = raw + rs7tn::Decoration(c->cents);
            Assign(c->raw, raw);
        } else {
            disp = rs7tn::MarkStock(Localise(disp0), g_state.names.prefix, suffix);
        }

        Assign(c->disp, disp);
        Log("tuning name [song list/details 0x56C950]: %s, CentOffset %d: stock \"%s\" / \"%s\" -> \"%s\" / \"%s\"",
            Offsets(c->t).c_str(), c->cents, raw0.c_str(), disp0.c_str(), raw.c_str(), disp.c_str());
    } catch (...) {
    }

    Close(c);
}

// ---- TU / TUT: 0x56C300 ----
extern "C" uint32_t __cdecl Rs7_TnUuidPre(void* out) {
    if (!g_rs7_tn_on || !out) {
        return 0;
    }

    return Open(nullptr, out, 0);
}

extern "C" void __cdecl Rs7_TnUuidTuning(void* tuning, void* attributes) {
    const DWORD me = GetCurrentThreadId();
    NameCall* c = nullptr;

    for (NameCall& x : g_state.calls) {
        if (x.used && x.thread == me && !x.raw && !x.seven) {
            c = &x;
        }
    }

    if (!c) {
        return;
    }

    int t[7];
    if (!ReadSeven(tuning, t)) {
        return;
    }

    double cents = 0.0;

    c->cents = JsonNumber(attributes, "CentOffset", &cents) && cents > -1200.0 && cents < 1200.0 ? int(cents) : 0;
    std::memcpy(c->t, t, sizeof t);
    c->seven = true;
    c->eight = JsonOffset(tuning, rs7tn::kKey8, &c->t8);  // an 8-string chart
}

// TUT - 0x56C408 in 0x56C300: EDI = the arrangement's Tuning object, [ebp-88h] = its Attributes.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TUT(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (g_rs7_tn_on) {
        Rs7_TnUuidTuning(reinterpret_cast<void*>(c.edi), reinterpret_cast<void*>(rs7tuner::Dword(c.ebp - 0x88)));
    }
}

extern "C" void __cdecl Rs7_TnUuidPost(uint32_t h) {
    NameCall* c = Get(h);
    if (!c) {
        return;
    }

    try {
        if (c->seven) {
            const std::string stock = StrRead(c->disp);
            const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
            std::string out = c->eight ? rs7tn::KnownDisplay8(t8, g_state.names, c->cents, true)
                                       : rs7tn::KnownDisplay(c->t, g_state.names, c->cents, true);

            if (out.empty()) {
                out = rs7tn::MarkStock(Localise(stock), g_state.names.prefix,
                                       c->eight ? rs7tn::Suffix8(g_state.names.suffix).c_str() : g_state.names.suffix);
            }
            Assign(c->disp, out);
            Log("tuning name [arrangement 0x56C300]: %s, CentOffset %d: stock \"%s\" -> \"%s\"", Offsets(c->t).c_str(),
                c->cents, stock.c_str(), out.c_str());
        }
    } catch (...) {
    }

    Close(c);
}

// ---- TN: 0x5B7750 ----
extern "C" uint32_t __cdecl Rs7_TnArrayPre(void* out, const int* offs, uint32_t* post) {
    if (post) {
        *post = 0;
    }
    if (!g_rs7_tn_on || !out || !offs) {
        return 0;
    }

    int t[7];
    if (!Rs7Tuner_SevenFor(offs, &t[6])) {
        return 0;
    }
    if (!rs7s7d::SafeCopy(t, offs, 6 * sizeof(int))) {
        return 0;
    }

    try {
        const std::string name = rs7tn::KnownDisplay(t, g_state.names, 0, false);

        if (name.empty()) {
            if (post) {
                *post = 1;  // the stock name, marked after the original
            }
            return 0;
        }

        if (!CallConstruct(out, name.data(), name.data() + name.size())) {
            return 0;
        }

        Log("tuning name [tuner 0x5B7750]: %s -> \"%s\"", Offsets(t).c_str(), name.c_str());
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" void __cdecl Rs7_TnArrayPost(void* out, uint32_t post) {
    if (!post || !out) {
        return;
    }

    try {
        const std::string stock = StrRead(out);
        const std::string marked = rs7tn::MarkStock(Localise(stock), g_state.names.prefix, g_state.names.suffix);

        Assign(out, marked);
        Log("tuning name [tuner 0x5B7750]: unknown 7-string tuning: stock \"%s\" -> \"%s\"", stock.c_str(),
            marked.c_str());
    } catch (...) {
    }
}
