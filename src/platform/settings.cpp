#include "platform/settings.h"
#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

// Every table's rows in kRsxrSettingTables order, and their state. Built at the first use (the tables are constant).
struct Flat {
    std::vector<const RsxrSetDef*> defs;
    std::vector<RsxrSetState> state;
};

Flat& All() {
    static Flat flat = [] {
        Flat f;

        for (int t = 0; t < kRsxrSettingTableCount; ++t) {
            for (int i = 0; i < kRsxrSettingTables[t]->count; ++i) {
                f.defs.push_back(&kRsxrSettingTables[t]->defs[i]);
            }
        }

        f.state.resize(f.defs.size());

        return f;
    }();

    return flat;
}

int Count() {
    return int(All().defs.size());
}

const RsxrSetDef& Def(int i) {
    return *All().defs[size_t(i)];
}

int Index(const char* id) {
    for (int i = 0; i < Count(); ++i) {
        if (!std::strcmp(Def(i).id, id)) {
            return i;
        }
    }

    return -1;
}

RsxrSetState& State(int i) {
    return All().state[size_t(i)];
}

const RsxrSetValue& Val(const char* id) {
    static const RsxrSetValue none{};
    const int i = Index(id);

    return i < 0 ? none : State(i).value;
}

const char* SkipBlanks(const char* p) {
    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    return p;
}

bool OnlyBlanks(const char* p) {
    return !*SkipBlanks(p);
}

bool ParseFloat(const char* text, double* out) {
    text = SkipBlanks(text);
    if (!*text) {
        return false;
    }

    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || !OnlyBlanks(end) || !std::isfinite(v)) {
        return false;
    }

    *out = v;

    return true;
}

bool ParseInt(const char* text, long* out) {
    text = SkipBlanks(text);
    if (!*text) {
        return false;
    }

    char* end = nullptr;

    errno = 0;
    const long v = std::strtol(text, &end, 10);
    if (end == text || !OnlyBlanks(end) || errno == ERANGE) {
        return false;
    }

    *out = v;

    return true;
}

bool InRange(const RsxrSetDef& d, double v) {
    return v >= double(d.lo) - 1e-6 && v <= double(d.hi) + 1e-6;
}

// Ini-text -> value (strict: the whole text, in range). False: out untouched.
bool Parse(const RsxrSetDef& d, const char* text, RsxrSetValue* out) {
    RsxrSetValue v{};

    switch (d.type) {
        case T::Bool: {
            long n = 0;
            if (!ParseInt(text, &n) || (n != 0 && n != 1)) {
                return false;
            }

            v.i = int(n);
            break;
        }
        case T::Int: {
            long n = 0;
            if (!ParseInt(text, &n) || !InRange(d, double(n))) {
                return false;
            }

            v.i = int(n);
            break;
        }
        case T::Float: {
            double f = 0;
            if (!ParseFloat(text, &f) || !InRange(d, f)) {
                return false;
            }

            v.f[0] = float(f);
            break;
        }
        case T::Rgb: {
            wchar_t w[32] = {};
            if (std::strlen(text) >= 31) {
                return false;
            }

            for (int k = 0; text[k]; ++k) {
                w[k] = wchar_t(static_cast<unsigned char>(text[k]));
            }

            if (!Rsxr_ParseRgb(w, &v.rgb)) {
                return false;
            }

            break;
        }
        case T::Text: {
            const char* b = SkipBlanks(text);
            size_t n = std::strlen(b);

            while (n && (b[n - 1] == ' ' || b[n - 1] == '\t')) {
                --n;
            }

            if (n >= size_t(kRsxrSetTextMax) || (d.hi > 0 && double(n) > double(d.hi))) {
                return false;  // hi: max length
            }

            for (size_t k = 0; k < n; ++k) {
                if (static_cast<unsigned char>(b[k]) < 0x20 || static_cast<unsigned char>(b[k]) > 0x7E) {
                    return false;
                }
            }

            if (n >= 2 && (b[0] == '"' || b[0] == '\'') && b[n - 1] == b[0]) {
                return false;  // the ini read strips surrounding quotes: such a value would not read back
            }

            std::memcpy(v.text, b, n);
            v.text[n] = 0;
            break;
        }
    }

    *out = v;

    return true;
}

bool Valid(const RsxrSetDef& d, const RsxrSetValue& v) {
    switch (d.type) {
        case T::Bool:
            return v.i == 0 || v.i == 1;
        case T::Int:
            return InRange(d, double(v.i));
        case T::Float:
            return std::isfinite(v.f[0]) && InRange(d, v.f[0]);
        case T::Rgb:
            return v.rgb <= 0xFFFFFF;
        case T::Text: {  // the same bounds as Parse(Text): terminated, ASCII, max length (d.hi)
            const void* z = std::memchr(v.text, 0, sizeof v.text);
            if (!z) {
                return false;
            }

            const size_t n = size_t(static_cast<const char*>(z) - v.text);
            if (n >= size_t(kRsxrSetTextMax) || (d.hi > 0 && double(n) > double(d.hi))) {
                return false;
            }

            for (size_t k = 0; k < n; ++k) {
                if (static_cast<unsigned char>(v.text[k]) < 0x20 || static_cast<unsigned char>(v.text[k]) > 0x7E) {
                    return false;
                }
            }

            return true;
        }
    }

    return false;
}

RsxrSetValue Default(const RsxrSetDef& d) {
    RsxrSetValue v{};

    Parse(d, d.def, &v);

    return v;
}

// Every character outside printable ASCII becomes 0x01, which no type accepts (Text allows 0x20..0x7E only), so a
// value with such a character is rejected instead of read with a stand-in character.
std::string Narrow(const std::wstring& w) {
    std::string s;

    for (wchar_t c : w) {
        s.push_back(c >= 0x20 && c < 0x7F ? char(c) : '\x01');
    }

    return s;
}

// The ini's text of a key ("" = missing), and whether it was there. *cut: longer than kRsxrIniValueMax, so its text is
// only a prefix and must not pass as the value (RsxrConfig_ReadInt rejects it the same way).
bool ReadIni(const RsxrSetDef& d, const wchar_t* ini, std::string* out, bool* cut) {
    std::wstring w;
    if (!RsxrConfig_ReadPresent(d.section, d.key, ini, &w, cut)) {
        return false;
    }

    *out = Narrow(w);

    return true;
}

// The shortest text that reads back as the same float (0.4f -> "0.4", not "0.400000006").
void FormatFloat(char* b, size_t n, float f) {
    for (int digits = 4; digits <= 9; ++digits) {
        std::snprintf(b, n, "%.*g", digits, double(f));

        if (std::strtof(b, nullptr) == f) {
            return;
        }
    }
}

// Why an ini value of a Bool / Int row did not parse, for the log.
const char* IntReason(const char* text, bool cut) {
    long n = 0;
    if (cut || !ParseInt(text, &n)) {
        return "is not a whole number";
    }

    return "is out of range";
}

// Windows' text for an error code, without the trailing line break.
std::wstring ErrorText(DWORD e) {
    wchar_t buf[256] = {};
    const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD n = FormatMessageW(flags, nullptr, e, 0, buf, 256, nullptr);

    while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L' ')) {
        buf[--n] = 0;
    }

    return buf;
}
}  // namespace

bool RsxrSet_Equal(RsxrSetType t, const RsxrSetValue& a, const RsxrSetValue& b) {
    switch (t) {
        case T::Bool:
        case T::Int:
            return a.i == b.i;
        case T::Float:
            return a.f[0] == b.f[0];
        case T::Rgb:
            return a.rgb == b.rgb;
        case T::Text:
            return std::strcmp(a.text, b.text) == 0;
    }

    return false;
}

int RsxrSet_Count() {
    return Count();
}

const RsxrSetDef& RsxrSet_Def(int i) {
    return Def(i);
}

RsxrSetState& RsxrSet_State(int i) {
    return State(i);
}

int RsxrSet_Find(const char* id) {
    if (!id) {
        return -1;
    }

    const int exact = Index(id);
    if (exact >= 0 || std::strchr(id, '.')) {
        return exact;
    }

    int found = -1;

    for (int i = 0; i < Count(); ++i) {
        const char* bare = std::strchr(Def(i).id, '.') + 1;

        if (!std::strcmp(bare, id)) {
            if (found >= 0) {
                return -1;  // ambiguous
            }

            found = i;
        }
    }

    return found;
}

void RsxrSet_ResetForTests() {
    for (int i = 0; i < Count(); ++i) {
        State(i) = RsxrSetState{};
        State(i).value = State(i).running = State(i).saved = Default(Def(i));
    }
}

bool RsxrSet_ReadIni(const wchar_t* iniPath) {
    const std::wstring path = RsxrConfig_ResolveIniPath(iniPath);
    const DWORD attr = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    const bool found = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);

    if (path.empty()) {
        RsxrLog("config: invalid/overlong ini path; defaults used");
    }

    for (int t = 0; t < kRsxrSettingTableCount; ++t) {
        if (kRsxrSettingTables[t]->read) {
            kRsxrSettingTables[t]->read(found ? path.c_str() : nullptr);
        }
    }

    return found;
}

int RsxrSet_ReadTableInt(const RsxrSetTable& t, const char* id, const wchar_t* iniPath) {
    for (int i = 0; i < t.count; ++i) {
        const RsxrSetDef& d = t.defs[i];
        if (std::strcmp(d.id, id) == 0) {
            return RsxrConfig_ReadInt(d.section, d.key, std::atoi(d.def), iniPath, int(d.hi));
        }
    }

    RsxrLog("config: %s has no settings row; disabled", id);

    return 0;
}

bool RsxrSet_ParseTableFloat(const RsxrSetTable& t, const char* id, const wchar_t* text, float* out) {
    *out = 0.0f;

    for (int i = 0; i < t.count; ++i) {
        const RsxrSetDef& d = t.defs[i];
        if (std::strcmp(d.id, id) != 0 || d.type != T::Float) {
            continue;
        }

        double def = 0, v = 0;
        char narrow[64] = {};
        size_t n = 0;

        ParseFloat(d.def, &def);
        *out = float(def);

        for (; text && text[n] && n + 1 < sizeof narrow; ++n) {
            if (text[n] > 0x7F) {
                return false;
            }

            narrow[n] = char(text[n]);
        }

        if ((text && text[n]) || !ParseFloat(narrow, &v) || !InRange(d, v)) {
            return false;
        }

        *out = float(v);
        return true;
    }

    return false;
}

bool RsxrSet_InitFromIni(const wchar_t* iniPath) {
    RsxrSet_ResetForTests();
    // Every feature's own startup read: the same validation and the same defaults as the game uses
    // (RsxrSetDef::startup). It logs an invalid path and hands every table its defaults.
    const bool found = RsxrSet_ReadIni(iniPath);
    const std::wstring path = RsxrConfig_ResolveIniPath(iniPath);

    if (path.empty()) {
        return false;
    }

    iniPath = path.c_str();

    for (int i = 0; i < Count(); ++i) {
        const RsxrSetDef& d = Def(i);
        RsxrSetState& s = State(i);
        std::string text;
        bool cut = false;

        s.fromIni = ReadIni(d, iniPath, &text, &cut);
        RsxrSetValue v = Default(d);

        if (s.fromIni && (cut || !Parse(d, text.c_str(), &v))) {
            if (d.type == T::Bool || d.type == T::Int) {
                // 0 (off) until fixed, as the feature's own read (RsxrConfig_ReadInt); the default if 0 is not allowed
                v = InRange(d, 0.0) ? RsxrSetValue{} : Default(d);
                RsxrLog("config: [%ls] %ls in rsxr.ini %s (a whole number from %g to %g); using %d until fixed "
                        "(invalid integer)",
                        d.section, d.key, IntReason(text.c_str(), cut), double(d.lo), double(d.hi), v.i);
            } else {
                v = Default(d);
            }
        }

        if (d.startup) {
            d.startup(v);
        }
        s.value = s.running = s.saved = v;
    }

    return found;
}

const char* RsxrSet_ResultName(RsxrSetResult r) {
    switch (r) {
        case RsxrSetResult::Live:
            return "live";
        case RsxrSetResult::NextSession:
            return "next session";
        case RsxrSetResult::Restart:
            return "restart required";
        case RsxrSetResult::Unchanged:
            return "unchanged";
        case RsxrSetResult::Invalid:
            return "invalid value";
        case RsxrSetResult::Unknown:
            return "unknown key";
    }

    return "?";
}

RsxrSetResult RsxrSet_Commit(int i, const RsxrSetValue& v) {
    if (i < 0 || i >= Count()) {
        return RsxrSetResult::Unknown;
    }

    const RsxrSetDef& d = Def(i);
    if (!Valid(d, v)) {
        return RsxrSetResult::Invalid;
    }

    RsxrSetState& s = State(i);
    if (RsxrSet_Equal(d.type, s.value, v) && (d.when == W::Restart || RsxrSet_Equal(d.type, s.running, v))) {
        return RsxrSetResult::Unchanged;
    }

    s.value = v;
    if (d.when == W::Restart) {
        return RsxrSetResult::Restart;
    }

    s.running = v;
    if (d.apply) {
        d.apply(v);
    }

    return d.when == W::Live ? RsxrSetResult::Live : RsxrSetResult::NextSession;
}

RsxrSetResult RsxrSet_SetText(const char* id, const char* text) {
    const int i = RsxrSet_Find(id);
    if (i < 0) {
        return RsxrSetResult::Unknown;
    }

    RsxrSetValue v{};
    if (!text || !Parse(Def(i), text, &v)) {
        return RsxrSetResult::Invalid;
    }

    return RsxrSet_Commit(i, v);
}

std::string RsxrSet_Format(int i, const RsxrSetValue& v) {
    char b[kRsxrSetTextMax + 8] = "";  // an unknown type formats as ""

    switch (Def(i).type) {
        case T::Bool:
        case T::Int:
            std::snprintf(b, sizeof b, "%d", v.i);
            break;
        case T::Float:
            FormatFloat(b, sizeof b, v.f[0]);
            break;
        case T::Rgb:
            std::snprintf(b, sizeof b, "%06X", unsigned(v.rgb));
            break;
        case T::Text:
            std::snprintf(b, sizeof b, "%s", v.text);
            break;
    }

    return b;
}

bool RsxrSet_Dirty(int i) {
    return !RsxrSet_Equal(Def(i).type, State(i).value, State(i).saved);
}

int RsxrSet_DirtyCount() {
    int n = 0;

    for (int i = 0; i < Count(); ++i) {
        n += RsxrSet_Dirty(i) ? 1 : 0;
    }

    return n;
}

bool RsxrSet_NeedsRestart(int i) {
    return Def(i).when == W::Restart && !RsxrSet_Equal(Def(i).type, State(i).value, State(i).running);
}

int RsxrSet_Save(const wchar_t* iniPath) {
    const std::wstring path = RsxrConfig_ResolveIniPath(iniPath);

    if (path.empty()) {
        RsxrLog("overlay: invalid/overlong ini path; settings not saved");
        return -1;
    }

    iniPath = path.c_str();
    int written = 0;

    for (int i = 0; i < Count(); ++i) {
        if (!RsxrSet_Dirty(i)) {
            continue;
        }

        const std::string text = RsxrSet_Format(i, State(i).value);
        const std::wstring w(text.begin(), text.end());

        if (!WritePrivateProfileStringW(Def(i).section, Def(i).key, w.c_str(), iniPath)) {
            const DWORD e = GetLastError();

            RsxrLog("overlay: saving [%ls] %ls failed: %ls (%lu)", Def(i).section, Def(i).key, ErrorText(e).c_str(), e);
            return -1;
        }

        State(i).saved = State(i).value;
        State(i).fromIni = true;
        ++written;
    }

    return written;
}

bool RsxrSet_Bool(const char* id) {
    return Val(id).i != 0;
}

int RsxrSet_Int(const char* id) {
    return Val(id).i;
}

float RsxrSet_Float(const char* id) {
    return Val(id).f[0];
}

uint32_t RsxrSet_Rgb(const char* id) {
    return Val(id).rgb;
}

const char* RsxrSet_Text(const char* id) {
    return Val(id).text;
}
