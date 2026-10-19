#include "platform/log.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace {
CRITICAL_SECTION g_cs;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
FILE* g_file = nullptr;
DWORD g_start = 0;
Rs7LogSink g_sink = nullptr;
// The last kTail lines (no timestamp) for the overlay's log panel.
constexpr unsigned kTail = 200, kTailLen = 240;
char g_tail[kTail][kTailLen];
unsigned long g_tailSeq = 0;  // lines ever added

// Set once by Rs7Log_Open (never freed); null = no redaction.
std::atomic<const Rs7LogRedactor*> g_redactor{nullptr};

BOOL CALLBACK InitCs(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_cs);

    return TRUE;
}

bool IsSep(char c) {
    return c == '\\' || c == '/';
}

bool IsWordChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// ASCII-lowercase; '/' counts as '\' so a path matches in either spelling.
char Fold(char c) {
    if (c == '/') {
        return '\\';
    }

    return (c >= 'A' && c <= 'Z') ? char(c + 'a' - 'A') : c;
}

bool MatchesAt(const std::string& s, size_t i, const std::string& needle) {
    if (needle.empty() || s.size() - i < needle.size()) {
        return false;
    }

    for (size_t k = 0; k < needle.size(); ++k) {
        if (Fold(s[i + k]) != Fold(needle[k])) {
            return false;
        }
    }

    return true;
}

// A name counts only as a whole path segment: it starts after a separator and ends at the end of the text, a
// separator, ':', whitespace, a quote or '='. "max 7 strings" and "maximum" are log text, "\max\" is a path.
bool NameFitsAt(const std::string& s, size_t i, const std::string& name) {
    if (i == 0 || !IsSep(s[i - 1])) {
        return false;
    }

    const size_t end = i + name.size();

    if (end == s.size()) {
        return true;
    }

    const char c = s[end];

    return IsSep(c) || c == ':' || c == '=' || c == '"' || c == '\'' || std::isspace(static_cast<unsigned char>(c));
}

// The replacement for the text at s[i..], with its length in *len; nullptr = nothing to hide here.
const char* HideAt(const std::string& s, size_t i, const Rs7LogRedactor& r, size_t* len) {
    for (const std::string& p : r.profiles) {
        if (MatchesAt(s, i, p) && (i + p.size() == s.size() || !IsWordChar(s[i + p.size()]))) {
            *len = p.size();

            return "%USERPROFILE%";
        }
    }

    for (const std::string& n : r.computers) {
        if (MatchesAt(s, i, n) && NameFitsAt(s, i, n)) {
            *len = n.size();

            return "<pc>";
        }
    }

    for (const std::string& n : r.users) {
        if (MatchesAt(s, i, n) && NameFitsAt(s, i, n)) {
            *len = n.size();

            return "<user>";
        }
    }

    return nullptr;
}

std::string Narrow(const std::wstring& w, UINT codePage) {
    if (w.empty()) {
        return {};
    }

    const int n = WideCharToMultiByte(codePage, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n > 0 ? n : 0), '\0');

    if (n > 0) {
        WideCharToMultiByte(codePage, 0, w.data(), int(w.size()), &out[0], n, nullptr, nullptr);
    }

    return out;
}

// `w` as UTF-8 and as ANSI (what %ls / %s of an ANSI API can print), without empties and duplicates.
void AddNames(std::vector<std::string>* list, const std::wstring& w) {
    for (const UINT cp : {UINT(CP_UTF8), UINT(CP_ACP)}) {
        const std::string s = Narrow(w, cp);

        if (!s.empty() && std::find(list->begin(), list->end(), s) == list->end()) {
            list->push_back(s);
        }
    }
}

std::wstring Env(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buf, DWORD(sizeof buf / sizeof buf[0]));

    return n && n < sizeof buf / sizeof buf[0] ? std::wstring(buf, n) : std::wstring();
}
}  // namespace

std::string Rs7Log_Redact(const std::string& msg, const Rs7LogRedactor& r) {
    std::string out;

    out.reserve(msg.size());

    for (size_t i = 0; i < msg.size();) {
        size_t len = 0;
        const char* hide = HideAt(msg, i, r, &len);

        if (hide) {
            out += hide;
            i += len;
        } else {
            out += msg[i++];
        }
    }

    return out;
}

Rs7LogRedactor Rs7Log_CurrentIdentity() {
    Rs7LogRedactor r;
    std::wstring profile = Env(L"USERPROFILE");

    while (!profile.empty() && (profile.back() == L'\\' || profile.back() == L'/')) {
        profile.pop_back();
    }

    // "C:" alone (a profile at a drive root) would hide every path on the drive.
    if (profile.size() > 3) {
        AddNames(&r.profiles, profile);
        AddNames(&r.users, profile.substr(profile.find_last_of(L"\\/") + 1));
    }

    AddNames(&r.users, Env(L"USERNAME"));
    AddNames(&r.computers, Env(L"COMPUTERNAME"));

    wchar_t pc[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = DWORD(sizeof pc / sizeof pc[0]);

    if (GetComputerNameW(pc, &n)) {
        AddNames(&r.computers, std::wstring(pc, n));
    }

    return r;
}

std::string Rs7Log_Format(unsigned ms, const char* msg) {
    char head[32];

    std::snprintf(head, sizeof head, "[%7u.%03u] ", ms / 1000, ms % 1000);

    return std::string(head) + msg + "\n";
}

void Rs7Log_Open(const std::wstring& path, bool redact) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);

    if (!g_file) {
        if (redact) {
            g_redactor.store(new Rs7LogRedactor(Rs7Log_CurrentIdentity()), std::memory_order_release);
        }

        g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);  // others may read it while the game runs
        g_start = GetTickCount();
    }

    LeaveCriticalSection(&g_cs);
}

void Rs7Log(const char* fmt, ...) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    // The one place personal data is removed: file, overlay panel and test sink all get the redacted text.
    // Formatted before taking the lock: if the allocation throws, the lock is not left held.
    const Rs7LogRedactor* redactor = g_redactor.load(std::memory_order_acquire);
    const std::string clean = redactor ? Rs7Log_Redact(msg, *redactor) : std::string();
    const char* text = redactor ? clean.c_str() : msg;
    const std::string line = Rs7Log_Format(GetTickCount() - g_start, text);

    EnterCriticalSection(&g_cs);
    std::snprintf(g_tail[g_tailSeq % kTail], kTailLen, "%s", line.c_str());
    ++g_tailSeq;
    if (g_sink) {
        g_sink(text);
    }

    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fflush(g_file);
    }

    LeaveCriticalSection(&g_cs);
}

void Rs7Log_SetSink(Rs7LogSink sink) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    g_sink = sink;
    LeaveCriticalSection(&g_cs);
}

unsigned long Rs7Log_Tail(char (*out)[240], unsigned max, unsigned* got) {
    InitOnceExecuteOnce(&g_once, InitCs, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    const unsigned long seq = g_tailSeq;
    const unsigned have = seq < kTail ? unsigned(seq) : kTail;
    const unsigned n = have < max ? have : max;

    for (unsigned i = 0; i < n; ++i) {
        const unsigned long idx = seq - n + i;

        std::memcpy(out[i], g_tail[idx % kTail], kTailLen);
    }

    LeaveCriticalSection(&g_cs);
    *got = n;

    return seq;
}
