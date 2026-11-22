#include "overlay/overlaycmd.h"
#include "platform/log.h"
#include <windows.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace {
constexpr size_t kMaxQueued = 64;

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();

    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
        ++b;
    }

    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
        --e;
    }

    return s.substr(b, e - b);
}

// First word of s (lower case) and the trimmed rest.
void Split(const std::string& s, std::string* word, std::string* rest) {
    const std::string t = Trim(s);
    size_t i = 0;

    while (i < t.size() && t[i] != ' ' && t[i] != '\t') {
        ++i;
    }

    *word = t.substr(0, i);

    for (char& c : *word) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }

    *rest = Trim(t.substr(i));
}

SRWLOCK g_lock = SRWLOCK_INIT;
std::deque<RsxrCmd>* g_queue;  // created on the first post (pipe thread)
}  // namespace

bool RsxrCmd_Parse(const char* line, RsxrCmd* out) {
    if (!line || line[0] != 'o' || (line[1] != ' ' && line[1] != '\t')) {
        return false;
    }

    std::string verb, rest;

    Split(line + 2, &verb, &rest);
    RsxrCmd c;

    if (verb == "toggle" || verb == "show" || verb == "hide" || verb == "save" || verb == "status") {
        if (!rest.empty()) {
            return false;
        }

        c.kind = verb == "toggle" ? RsxrCmdKind::Toggle
                 : verb == "show" ? RsxrCmdKind::Show
                 : verb == "hide" ? RsxrCmdKind::Hide
                 : verb == "save" ? RsxrCmdKind::Save
                                  : RsxrCmdKind::Status;
    } else if (verb == "tab") {
        std::string name, extra;

        Split(rest, &name, &extra);
        if (name.empty() || !extra.empty()) {
            return false;
        }

        c.kind = RsxrCmdKind::Tab;
        c.key = name;
    } else if (verb == "set") {
        std::string key, value;

        Split(rest, &key, &value);
        if (key.empty() || value.empty()) {
            return false;
        }

        c.kind = RsxrCmdKind::Set;
        c.key = key;
        c.value = value;
    } else {
        return false;
    }

    *out = c;

    return true;
}

bool RsxrCmd_Post(const char* line) {
    RsxrCmd c;

    if (!RsxrCmd_Parse(line, &c)) {
        RsxrLog("overlay: cmd '%s' not understood", line ? line : "");
        return false;
    }

    bool ok = false;
    bool oom = false;  // allocation failed, as opposed to a full queue

    AcquireSRWLockExclusive(&g_lock);

    try {
        if (!g_queue) {
            g_queue = new std::deque<RsxrCmd>();
        }

        if (g_queue->size() < kMaxQueued) {
            g_queue->push_back(c);
            ok = true;
        }
    } catch (...) {
        ok = false;
        oom = true;
    }

    ReleaseSRWLockExclusive(&g_lock);
    if (!ok) {
        RsxrLog("overlay: cmd '%s' dropped (%s)", line, oom ? "out of memory" : "queue full");
    }

    return ok;
}

bool RsxrCmd_Pop(RsxrCmd* out) {
    bool got = false;

    AcquireSRWLockExclusive(&g_lock);

    try {
        if (g_queue && !g_queue->empty()) {
            *out = g_queue->front();
            g_queue->pop_front();
            got = true;
        }
    } catch (...) {
        got = false;
    }

    ReleaseSRWLockExclusive(&g_lock);

    return got;
}

int Rsxr_ParseHotkey(const char* text) {
    if (!text) {
        return 0;
    }

    std::string t = Trim(text);

    for (char& c : t) {
        c = char(std::toupper(static_cast<unsigned char>(c)));
    }

    if (t.empty()) {
        return 0;
    }

    if (t.size() > 2 && t[0] == '0' && t[1] == 'X') {
        char* end = nullptr;
        const long v = std::strtol(t.c_str() + 2, &end, 16);

        return *end || v <= 0 || v > 0xFE ? 0 : int(v);
    }

    if (t.size() == 1 && ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9'))) {
        return t[0];
    }

    if (t[0] == 'F' && t.size() <= 3) {
        char* end = nullptr;
        const long n = std::strtol(t.c_str() + 1, &end, 10);
        if (!*end && n >= 1 && n <= 24) {
            return VK_F1 + int(n) - 1;
        }

        return 0;
    }

    static const struct {
        const char* name;
        int vk;
    } kNames[] = {
        {"INSERT", VK_INSERT},   {"DELETE", VK_DELETE}, {"HOME", VK_HOME},   {"END", VK_END},
        {"PGUP", VK_PRIOR},      {"PGDN", VK_NEXT},     {"PAUSE", VK_PAUSE}, {"SCROLL", VK_SCROLL},
        {"NUMLOCK", VK_NUMLOCK}, {"OEM_3", VK_OEM_3},
    };

    for (const auto& k : kNames) {
        if (t == k.name) {
            return k.vk;
        }
    }

    return 0;
}
