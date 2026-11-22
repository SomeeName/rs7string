#include "features/string7/string7.h"  // g_rsxr_frame_global (X_DT): *0x135FBFC, the localisation context
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuningname.h"
#include "model/gamestring.h"
#include "model/manifest.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>

// Extended tuning names, the game side (TJ TU TUT TN, tuningname.h): reads the arrangement's Tuning, asks the
// name logic (tuningname.cpp) and writes the game's strings through its own string routines (callref sites).

extern "C" {
__declspec(dllexport) uintptr_t g_rsxr_tn_on = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_strcon = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_strasg = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_loc = 0;
__declspec(dllexport) uintptr_t g_rsxr_fn_strdtor = 0;
}

thread_local rsxrtuner::NameCalls rsxrtuner::g_nameCalls{};

namespace {
using rsxr::JsonNumber;
using rsxr::JsonOffset;
using rsxrtuner::g_nameCalls;
using rsxrtuner::g_state;
using rsxrtuner::NameCall;
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

        RsxrLog("%s", buf);
    } catch (...) {
    }
}

// string0..5 of a Tuning object into t[0..5]; false when one is missing.
bool ReadSix(void* tuning, int t[6]) {
    char key[16];

    for (int s = 0; s < 6; ++s) {
        std::snprintf(key, sizeof key, "string%d", s);
        if (!JsonOffset(tuning, key, &t[s])) {
            return false;
        }
    }

    return true;
}

// A Tuning object with RsxrString6 and all of string0..5: t[0..6]. Anything else: false (stock).
bool ReadSeven(void* tuning, int t[7]) {
    return JsonOffset(tuning, rsxrtn::kKey, &t[6]) && ReadSix(tuning, t);
}

// A Tuning object with RsxrString4 (+ RsxrString5) and all of string0..5: t[0..5], returns 5 / 6. Anything else: 0.
int ReadBass(void* tuning, int t[6]) {
    int b = 0;

    if (!JsonOffset(tuning, rsxrtn::kKeyBass4, &b) || !ReadSix(tuning, t)) {
        return 0;
    }

    return JsonOffset(tuning, rsxrtn::kKeyBass5, &b) ? 6 : 5;
}

// The game's 0x18-byte string: inline 16 bytes at +0 while +14h == &+10h, else the data pointer
// at +0; +10h = end.
std::string StrRead(const void* s) {
    char text[1025];
    size_t length = 0;

    return rsxr::ReadGameString(s, text, sizeof text, false, &length) ? std::string(text, length) : std::string();
}

// The game's string routines (callref sites). Custom register ABIs: every register saved around them.
bool CallAssign(void* s, const char* b, const char* e) {
    const uintptr_t fn = g_rsxr_fn_strasg;
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
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

bool CallConstruct(void* s, const char* b, const char* e) {
    const uintptr_t fn = g_rsxr_fn_strcon;
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
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

bool CallLocalise(void* dst, const char* text) {
    const uintptr_t fn = g_rsxr_fn_loc;
    uint8_t* app = g_rsxr_frame_global ? *reinterpret_cast<uint8_t**>(g_rsxr_frame_global) : nullptr;
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
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        return false;
    }
}

void CallDestroy(void* s) {
    const uintptr_t fn = g_rsxr_fn_strdtor;
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
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }
}

bool Assign(void* s, const std::string& v) {
    return CallAssign(s, v.data(), v.data() + v.size());
}

// The string count of a named arrangement: 5 / 6 extended bass, 8, else 7.
int StringCount(int bass, bool eight) {
    return bass ? bass : eight ? 8 : 7;
}

// The configured mark for an arrangement of `strings` strings: the 7-string suffix as set, else SuffixN.
std::string MarkFor(int strings) {
    return strings == 7 ? std::string(g_state.names.suffix) : rsxrtn::SuffixN(g_state.names.suffix, strings);
}

// The known name's display text for t[0..7] (t[7] only with `eight`), or empty.
std::string KnownFor(const int t[8], int bass, bool eight, int cents, bool decorate) {
    return bass    ? rsxrtn::KnownDisplayBass(t, bass, g_state.names, cents, decorate)
           : eight ? rsxrtn::KnownDisplay8(t, g_state.names, cents, decorate)
                   : rsxrtn::KnownDisplay(t, g_state.names, cents, decorate);
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
    for (int i = 0; i < rsxrtuner::kNameCalls; ++i) {
        if (!g_nameCalls.entries[i].used) {
            NameCall& c = g_nameCalls.entries[i];

            c = NameCall{};
            c.previous = g_nameCalls.top;
            c.raw = raw;
            c.disp = disp;
            c.cents = cents;
            c.used = true;
            g_nameCalls.top = uint32_t(i + 1);
            return uint32_t(i + 1);
        }
    }

    return 0;  // all busy: stock
}

NameCall* Get(uint32_t h) {
    return (h >= 1 && h <= rsxrtuner::kNameCalls && g_nameCalls.entries[h - 1].used) ? &g_nameCalls.entries[h - 1]
                                                                                     : nullptr;
}

void Close(NameCall* c) {
    if (c) {
        uint32_t* link = &g_nameCalls.top;

        while (*link) {
            NameCall& active = g_nameCalls.entries[*link - 1];

            if (&active == c) {
                *link = c->previous;
                break;
            }

            link = &active.previous;
        }

        c->used = false;
    }
}
}  // namespace

void RsxrTn_Configure(const rsxrtn::Config& c) {
    g_state.names = c;
}

// ---- TJ: 0x56C950 ----
extern "C" uint32_t __cdecl Rsxr_TnJsonPre(void* raw, void* disp, void* json, int cents) {
    if (!g_rsxr_tn_on || !json) {
        return 0;
    }

    int t[7] = {};
    const bool seven = ReadSeven(json, t);
    const int bass = seven ? 0 : ReadBass(json, t);  // a 5/6-string bass chart

    if (!seven && !bass) {
        return 0;
    }

    const uint32_t h = Open(raw, disp, cents);

    if (NameCall* c = Get(h)) {
        c->seven = true;
        c->bass = bass;
        c->eight = seven && JsonOffset(json, rsxrtn::kKey8, &c->t8);  // an 8-string chart
        std::memcpy(c->t, t, sizeof t);
    }

    return h;
}

extern "C" void __cdecl Rsxr_TnJsonPost(uint32_t h) {
    NameCall* c = Get(h);
    if (!c) {
        return;
    }

    try {
        const std::string raw0 = StrRead(c->raw), disp0 = StrRead(c->disp);
        const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
        const std::string known = KnownFor(t8, c->bass, c->eight, 0, false);
        std::string raw = raw0, disp;

        if (!known.empty()) {
            // Raw token (row +150h, the tuning sort's group equality) and the display text: the
            // extended name; the display decorated like a matched stock name.
            raw = known;
            disp = raw + rsxrtn::Decoration(c->cents);
            Assign(c->raw, raw);
        } else {
            const std::string mark = MarkFor(StringCount(c->bass, c->eight));

            disp = rsxrtn::MarkStock(Localise(disp0), g_state.names.prefix, mark.c_str());
        }

        Assign(c->disp, disp);
        Log("tuning name [song list/details 0x56C950]: %s, CentOffset %d: stock \"%s\" / \"%s\" -> \"%s\" / \"%s\"",
            Offsets(c->t).c_str(), c->cents, raw0.c_str(), disp0.c_str(), raw.c_str(), disp.c_str());
    } catch (...) {
    }

    Close(c);
}

// ---- TU / TUT: 0x56C300 ----
extern "C" uint32_t __cdecl Rsxr_TnUuidPre(void* out) {
    if (!g_rsxr_tn_on || !out) {
        return 0;
    }

    return Open(nullptr, out, 0);
}

extern "C" void __cdecl Rsxr_TnUuidTuning(void* tuning, void* attributes) {
    NameCall* c = nullptr;

    for (uint32_t h = g_nameCalls.top; h; h = g_nameCalls.entries[h - 1].previous) {
        NameCall& x = g_nameCalls.entries[h - 1];

        if (!x.raw && !x.seven) {
            c = &x;
            break;
        }
    }

    if (!c) {
        return;
    }

    int t[7] = {};
    const bool seven = ReadSeven(tuning, t);
    const int bass = seven ? 0 : ReadBass(tuning, t);  // a 5/6-string bass chart

    if (!seven && !bass) {
        return;
    }

    double cents = 0.0;

    c->cents = JsonNumber(attributes, "CentOffset", &cents) && cents > -1200.0 && cents < 1200.0 ? int(cents) : 0;
    std::memcpy(c->t, t, sizeof t);
    c->seven = true;
    c->bass = bass;
    c->eight = seven && JsonOffset(tuning, rsxrtn::kKey8, &c->t8);  // an 8-string chart
}

// TUT - 0x56C408 in 0x56C300: EDI = the arrangement's Tuning object, [ebp-88h] = its Attributes.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_TUT(RsxrMid::Context& c) {
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    if (g_rsxr_tn_on) {
        Rsxr_TnUuidTuning(reinterpret_cast<void*>(c.edi), reinterpret_cast<void*>(rsxrtuner::Dword(c.ebp - 0x88)));
    }
}

extern "C" void __cdecl Rsxr_TnUuidPost(uint32_t h) {
    NameCall* c = Get(h);
    if (!c) {
        return;
    }

    try {
        if (c->seven) {
            const std::string stock = StrRead(c->disp);
            const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
            std::string out = KnownFor(t8, c->bass, c->eight, c->cents, true);

            if (out.empty()) {
                const std::string mark = MarkFor(StringCount(c->bass, c->eight));

                out = rsxrtn::MarkStock(Localise(stock), g_state.names.prefix, mark.c_str());
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
extern "C" uint32_t __cdecl Rsxr_TnArrayPre(void* out, const int* offs, uint32_t* post) {
    if (post) {
        *post = 0;
    }
    if (!g_rsxr_tn_on || !out || !offs) {
        return 0;
    }

    int t[8] = {};
    const bool seven = RsxrTuner_SevenFor(offs, &t[6]);
    const int bass = seven ? 0 : RsxrTuner_BassFor(offs);  // a 5/6-string bass arrangement

    if (!seven && !bass) {
        return 0;
    }
    if (!Rsxr_SafeCopyZero(t, offs, 6 * sizeof(int))) {
        return 0;
    }

    const bool eight = seven && RsxrTuner_EighthFor(offs, &t[7]);  // an 8-string arrangement: its own name (Name8)

    try {
        const std::string name = KnownFor(t, bass, eight, 0, false);

        if (name.empty()) {
            if (post) {
                // The stock name, marked after the original: the string count (7 / 8, 5 / 6 bass; never 0).
                *post = uint32_t(StringCount(bass, eight));
            }

            return 0;
        }

        if (!CallConstruct(out, name.data(), name.data() + name.size())) {
            return 0;
        }

        char eighth[24] = "";

        if (eight) {
            std::snprintf(eighth, sizeof eighth, " | 8th %d", t[7]);
        }
        Log("tuning name [tuner 0x5B7750]: %s%s -> \"%s\"", Offsets(t).c_str(), eighth, name.c_str());
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" void __cdecl Rsxr_TnArrayPost(void* out, uint32_t post) {
    if (!post || !out) {
        return;
    }

    try {
        const std::string stock = StrRead(out);
        const std::string mark = MarkFor(int(post));
        const std::string marked = rsxrtn::MarkStock(Localise(stock), g_state.names.prefix, mark.c_str());

        Assign(out, marked);
        Log("tuning name [tuner 0x5B7750]: unknown tuning (%d strings): stock \"%s\" -> \"%s\"", int(post),
            stock.c_str(), marked.c_str());
    } catch (...) {
    }
}
