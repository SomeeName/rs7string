#include "core/tuningname.h"
#include "core/eight.h"
#include "core/string7.h"      // g_rs7_frame_global (X_DT): *0x135FBFC, the localisation context
#include "core/string7diag.h"  // rs7s7d::SafeCopy
#include "core/tuner.h"        // Rs7Tuner_SevenFor
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_eight = 0;
__declspec(dllexport) uintptr_t g_rs7_tn_on = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strcon = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strasg = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_loc = 0;
__declspec(dllexport) uintptr_t g_rs7_fn_strdtor = 0;
}

// ---- the name table (pure) ----
namespace {
struct Family {
    int k;             // offset of strings 0..5 (and of the 7th string for Standard)
    const char* std;   // "<B+k> Standard"
    const char* drop;  // the 7th string two semitones lower
};

// k = +1 (C) .. -9 (D), the owner's list (issue #15); flats as the game's "Eb Standard" /
// "Eb Drop Db" (no C#/Db in this range except the Db of Eb Drop Db).
constexpr Family kFamilies[] = {
    {+1, "C Standard", "C Drop Bb"},   {0, "B Standard", "Drop A"},       {-1, "Bb Standard", "Bb Drop Ab"},
    {-2, "A Standard", "A Drop G"},    {-3, "Ab Standard", "Ab Drop Gb"}, {-4, "G Standard", "G Drop F"},
    {-5, "Gb Standard", "Gb Drop E"},  {-6, "F Standard", "F Drop Eb"},   {-7, "E Standard", "E Drop D"},
    {-8, "Eb Standard", "Eb Drop Db"}, {-9, "D Standard", "D Drop C"},
};

struct Alternate {
    int t[7];  // string0..5, Rs7String6
    const char* name;
};

// Low-to-high notes of the issue -> offsets from B1 E2 A2 D3 G3 B3 E4 (the 7th string first in
// the issue's lists, last here). Extended Drop D/C/B: a six-string Drop D / D Drop C / C# Drop B
// with the 7th string a fourth below string 0. Double Drop D and Open G7: the issue gives pitch
// classes only; these registers (D2 A2 D3 G3 B3 D4 D4, G1 D2 G2 B2 D3 F3 G3) are a HYPOTHESIS
// (docs/ARCHITECTURE.md) until a chart or the owner confirms them.
constexpr Alternate kAlternates[] = {
    {{-2, 0, 0, 0, 0, 0, -2}, "Extended Drop D"},      {{-4, -2, -2, -2, -2, -2, -4}, "Extended Drop C"},
    {{-5, -3, -3, -3, -3, -3, -5}, "Extended Drop B"}, {{5, 5, 5, 4, 3, -2, 3}, "Double Drop D"},
    {{-2, -2, -3, -5, -6, -9, -4}, "Open G7"},
};

bool IsNoteWithAccidental(const std::string& w) {
    return w.size() == 2 && w[0] >= 'A' && w[0] <= 'G' && (w[1] == 'b' || w[1] == '#');
}
}  // namespace

const char* rs7tn::Name7(const int t[7]) {
    if (!t) {
        return nullptr;
    }

    for (const Alternate& a : kAlternates) {
        if (std::memcmp(a.t, t, sizeof a.t) == 0) {
            return a.name;
        }
    }

    for (int s = 1; s < 6; ++s) {
        if (t[s] != t[0]) {
            return nullptr;
        }
    }

    for (const Family& f : kFamilies) {
        if (t[0] != f.k) {
            continue;
        }
        if (t[6] == f.k) {
            return f.std;
        }
        if (t[6] == f.k - 2) {
            return f.drop;
        }

        return nullptr;
    }

    return nullptr;
}

const char* rs7tn::Name8(const int t[8]) {
    if (!t) {
        return nullptr;
    }

    for (int s = 1; s < 7; ++s) {
        if (t[s] != t[0]) {
            return nullptr;
        }
    }

    // Rocksmith spelling: retain F# for the canonical instrument, flats for other families.
    static constexpr Family families[] = {
        {1, "G Standard", "G Drop F"},     {0, "F# Standard", "Drop E"},      {-1, "F Standard", "F Drop Eb"},
        {-2, "E Standard", "E Drop D"},    {-3, "Eb Standard", "Eb Drop Db"}, {-4, "D Standard", "D Drop C"},
        {-5, "Db Standard", "Db Drop B"},  {-6, "C Standard", "C Drop Bb"},   {-7, "B Standard", "B Drop A"},
        {-8, "Bb Standard", "Bb Drop Ab"}, {-9, "A Standard", "A Drop G"}};

    for (const Family& f : families) {
        if (t[0] != f.k) {
            continue;
        }
        if (t[7] == f.k) {
            return f.std;
        }
        if (t[7] == f.k - 2) {
            return f.drop;
        }

        return nullptr;
    }

    return nullptr;
}

std::string rs7tn::KnownDisplay8(const int t[8], const Config& c, int cents, bool decorate) {
    if (!c.on) {
        return {};
    }

    const char* name = Name8(t);
    if (!name) {
        return {};
    }

    std::string out = Display(Compose(name, c.prefix, Suffix8(c.suffix).c_str()));

    if (decorate) {
        out += Decoration(cents);
    }

    return out;
}

std::string rs7tn::Suffix8(const char* suffix7) {
    std::string s = suffix7 ? suffix7 : "";
    const size_t i = s.find('7');
    if (i == std::string::npos) {
        return "8Str";
    }

    s[i] = '8';

    return s;
}

std::string rs7tn::Compose(const char* base, const char* prefix, const char* suffix) {
    std::string out;

    if (prefix && *prefix) {
        out = std::string(prefix) + " ";
    }
    out += base ? base : "";
    if (suffix && *suffix) {
        out += std::string(" ") + suffix;
    }

    return out;
}

std::string rs7tn::Display(const std::string& name) {
    std::string out;
    size_t i = 0;

    while (i < name.size()) {
        if (name[i] == ' ') {
            out += ' ';
            ++i;
            continue;
        }

        const size_t j = name.find(' ', i);
        const std::string w = name.substr(i, j == std::string::npos ? std::string::npos : j - i);

        if (IsNoteWithAccidental(w)) {
            out += w[0];
            out += w[1] == 'b' ? "\xE2\x99\xAD" : "\xE2\x99\xAF";  // U+266D flat, U+266F sharp
        } else {
            for (char c : w) {
                out += (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
            }
        }

        i += w.size();
    }

    return out;
}

int rs7tn::ReferenceA(int cents) {
    if (!cents) {
        return 440;
    }

    const float x = static_cast<float>(static_cast<double>(cents) / 1200.0);  // FILD, FDIV, FSTP float
    const float p = std::pow(2.0f, x);                                        // 0x4E4AF0: float pow
    const float a = static_cast<float>(static_cast<double>(p) * 440.0);       // FMUL double, FSTP float

    return static_cast<int>(a);  // 0xF5B0D0: truncation
}

std::string rs7tn::Decoration(int cents) {
    if (!cents) {
        return {};
    }

    char b[24];

    std::snprintf(b, sizeof b, ": A%d", ReferenceA(cents));

    return b;
}

std::string rs7tn::MarkStock(const std::string& stock, const char* prefix, const char* suffix) {
    // The stock decoration ": A<digits>" at the end stays at the end.
    std::string name = stock, tail;
    const size_t p = stock.rfind(": A");

    if (p != std::string::npos && p + 3 < stock.size()) {
        bool digits = true;

        for (size_t i = p + 3; i < stock.size(); ++i) {
            digits = digits && stock[i] >= '0' && stock[i] <= '9';
        }

        if (digits) {
            name = stock.substr(0, p);
            tail = stock.substr(p);
        }
    }

    std::string out;

    if (prefix && *prefix) {
        out = Display(prefix) + " ";
    }
    out += name;
    if (suffix && *suffix) {
        out += " " + Display(suffix);
    }

    return out + tail;
}

std::string rs7tn::KnownDisplay(const int t[7], const Config& c, int cents, bool decorate) {
    if (!c.on) {
        return {};
    }

    const char* n = Name7(t);
    if (!n) {
        return {};
    }

    std::string out = Display(Compose(n, c.prefix, c.suffix));

    if (decorate) {
        out += Decoration(cents);
    }

    return out;
}

// ---- the game side ----
namespace {
rs7tn::Config g_cfg;
volatile long g_logs = 0;
constexpr long kMaxLogs = 60;
// The song list is rebuilt often: each distinct line is logged once (FNV-1a hashes of the last 64).
uint32_t g_seen[64];
long g_seenNext = 0;

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

        for (uint32_t x : g_seen) {
            if (x == h) {
                return;
            }
        }

        g_seen[_InterlockedIncrement(&g_seenNext) % 64] = h;
        if (_InterlockedIncrement(&g_logs) > kMaxLogs) {
            return;
        }

        Rs7Log("%s", buf);
    } catch (...) {
    }
}

// The game's JSON objects (as 0x56C950 / 0x889C50 read them): vtable +14h find(name) -> node or 0,
// node +50h -> number object or 0, the double at +8.
using JsonFind = void*(__thiscall*)(void* self, const char* name);
using JsonNum = uint8_t*(__thiscall*)(void* self);

bool JsonNumber(void* obj, const char* key, double* out) {
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool OffsetValue(void* obj, const char* key, int* out) {
    double d = 0.0;
    if (!JsonNumber(obj, key, &d) || !(d > -25.0 && d < 25.0)) {
        return false;
    }

    *out = static_cast<int>(d);  // the stock readers truncate (0xF5B0D0)

    return true;
}

// A Tuning object with Rs7String6 and all of string0..5: t[0..6]. Anything else: false (stock).
bool ReadSeven(void* tuning, int t[7]) {
    if (!OffsetValue(tuning, rs7tn::kKey, &t[6])) {
        return false;
    }

    char key[16];

    for (int s = 0; s < 6; ++s) {
        std::snprintf(key, sizeof key, "string%d", s);
        if (!OffsetValue(tuning, key, &t[s])) {
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

// Open calls (TJ / TU): Pre fills one, Post consumes it. A handful at most at a time.
struct Call {
    volatile long used;
    DWORD thread;
    void* raw;
    void* disp;  // TJ display output / TU output
    int cents;
    bool seven;  // TU: TUT saw a 7-string Tuning
    int t[7];
    int t8 = 0;
    bool eight = false;
};

constexpr int kCalls = 8;
Call g_calls[kCalls];

uint32_t Open(void* raw, void* disp, int cents) {
    for (int i = 0; i < kCalls; ++i) {
        if (_InterlockedCompareExchange(&g_calls[i].used, 1, 0) == 0) {
            Call& c = g_calls[i];

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

Call* Get(uint32_t h) {
    return (h >= 1 && h <= kCalls && g_calls[h - 1].used) ? &g_calls[h - 1] : nullptr;
}

void Close(Call* c) {
    if (c) {
        _InterlockedExchange(&c->used, 0);
    }
}
}  // namespace

void Rs7Tn_Configure(const rs7tn::Config& c) {
    g_cfg = c;
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

    if (Call* c = Get(h)) {
        c->seven = true;
        c->eight = OffsetValue(json, rs7tn::kKey8, &c->t8);  // an 8-string chart
        std::memcpy(c->t, t, sizeof t);
    }

    return h;
}

extern "C" void __cdecl Rs7_TnJsonPost(uint32_t h) {
    Call* c = Get(h);
    if (!c) {
        return;
    }

    try {
        const std::string raw0 = StrRead(c->raw), disp0 = StrRead(c->disp);
        const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
        const char* n = c->eight ? rs7tn::Name8(t8) : rs7tn::Name7(c->t);
        const std::string suffix8 = rs7tn::Suffix8(g_cfg.suffix);
        const char* suffix = c->eight ? suffix8.c_str() : g_cfg.suffix;
        std::string raw = raw0, disp;

        if (n) {
            // Raw token (row +150h, the tuning sort's group equality) and the display text: the
            // 7-string name; the display decorated like a matched stock name.
            raw = rs7tn::Display(rs7tn::Compose(n, g_cfg.prefix, suffix));
            disp = raw + rs7tn::Decoration(c->cents);
            Assign(c->raw, raw);
        } else {
            disp = rs7tn::MarkStock(Localise(disp0), g_cfg.prefix, suffix);
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
    Call* c = nullptr;

    for (Call& x : g_calls) {
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
    c->eight = OffsetValue(tuning, rs7tn::kKey8, &c->t8);  // an 8-string chart
}

extern "C" void __cdecl Rs7_TnUuidPost(uint32_t h) {
    Call* c = Get(h);
    if (!c) {
        return;
    }

    try {
        if (c->seven) {
            const std::string stock = StrRead(c->disp);
            const int t8[8] = {c->t[0], c->t[1], c->t[2], c->t[3], c->t[4], c->t[5], c->t[6], c->t8};
            std::string out = c->eight ? rs7tn::KnownDisplay8(t8, g_cfg, c->cents, true)
                                       : rs7tn::KnownDisplay(c->t, g_cfg, c->cents, true);

            if (out.empty()) {
                out = rs7tn::MarkStock(Localise(stock), g_cfg.prefix,
                                       c->eight ? rs7tn::Suffix8(g_cfg.suffix).c_str() : g_cfg.suffix);
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
        const std::string name = rs7tn::KnownDisplay(t, g_cfg, 0, false);

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
        const std::string marked = rs7tn::MarkStock(Localise(stock), g_cfg.prefix, g_cfg.suffix);

        Assign(out, marked);
        Log("tuning name [tuner 0x5B7750]: unknown 7-string tuning: stock \"%s\" -> \"%s\"", stock.c_str(),
            marked.c_str());
    } catch (...) {
    }
}
