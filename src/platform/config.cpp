#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <cerrno>
#include <cwchar>
#include <cwctype>

namespace {
// One more than the longest value plus its terminator: GetPrivateProfileString returns nSize - 1 both for an exact fit
// and for a cut value, so a value of exactly kRs7IniValueMax characters must stay below that count.
constexpr DWORD kReadSize = kRs7IniValueMax + 2;

// The text of [section] key, or `fallback` if the key is missing. *tooLong: longer than kRs7IniValueMax (the text is
// then only a prefix).
std::wstring Read(const wchar_t* section, const wchar_t* key, const wchar_t* fallback, const wchar_t* iniPath,
                  bool* tooLong) {
    wchar_t buf[kReadSize] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, fallback, buf, kReadSize, iniPath);

    *tooLong = n > DWORD(kRs7IniValueMax);

    return std::wstring(buf, n);
}

bool Blank(wchar_t ch) {
    return ch == L' ' || ch == L'\t';
}

int HexDigit(wchar_t ch) {  // ASCII only (iswxdigit would depend on the locale)
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return ch - L'a' + 10;
    }
    if (ch >= L'A' && ch <= L'F') {
        return ch - L'A' + 10;
    }

    return -1;
}
}  // namespace

int Rs7Config_ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* iniPath, int hi) {
    std::wstring text;
    bool tooLong = false;
    if (!Rs7Config_ReadPresent(section, key, iniPath, &text, &tooLong)) {
        return def;
    }

    const wchar_t* buf = text.c_str();
    wchar_t* end = nullptr;

    errno = 0;
    const long v = std::wcstol(buf, &end, 10);

    while (std::iswspace(*end)) {
        ++end;
    }

    const bool number = !tooLong && end != buf && !text.empty() && !*end && errno != ERANGE;

    if (!number || v < 0 || v > hi) {
        Rs7Log("config: [%ls] %ls in rs7string.ini %s (a whole number from 0 to %d); using 0 until fixed "
               "(invalid integer)",
               section, key, number ? "is out of range" : "is not a whole number", hi);
        return 0;
    }

    return int(v);
}

std::wstring Rs7Config_ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, bool* tooLong) {
    return Read(section, key, L"", iniPath, tooLong);
}

bool Rs7Config_ReadPresent(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, std::wstring* out,
                           bool* tooLong) {
    bool cut = false;
    std::wstring text = Read(section, key, L"\x01", iniPath, &cut);
    if (text.size() == 1 && text[0] == 1) {
        return false;
    }

    *out = std::move(text);
    if (tooLong) {
        *tooLong = cut;
    }

    return true;
}

std::wstring Rs7Config_ResolveIniPath(const wchar_t* iniPath) {
    if (!iniPath || !*iniPath) {
        return {};
    }

    const DWORD need = GetFullPathNameW(iniPath, 0, nullptr, nullptr);
    if (!need || need > 32768) {
        return {};
    }

    std::wstring path(need, L'\0');
    const DWORD n = GetFullPathNameW(iniPath, need, &path[0], nullptr);
    if (!n || n >= need) {
        return {};
    }

    path.resize(n);

    return path;
}

bool Rs7_ParseRgb(const wchar_t* t, uint32_t* rgb) {
    if (!t) {
        return false;
    }

    while (Blank(*t)) {
        ++t;
    }

    if (*t == L'#') {
        ++t;
    }
    uint32_t v = 0;
    int n = 0;

    for (; *t && !Blank(*t); ++t, ++n) {
        const int d = HexDigit(*t);
        if (d < 0 || n >= 6) {
            return false;
        }

        v = v * 16 + static_cast<uint32_t>(d);
    }

    while (Blank(*t)) {
        ++t;
    }

    if (n != 6 || *t) {
        return false;
    }

    *rgb = v;

    return true;
}
