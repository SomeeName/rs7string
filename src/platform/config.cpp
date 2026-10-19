#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <cerrno>
#include <cwchar>
#include <cwctype>

namespace {
// Long enough for any sensible value; a longer one is not cut short (a truncated "C8C8C8   junk"
// would parse): *tooLong is set and the caller treats the value as invalid.
constexpr DWORD kValueMax = 256;

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* ini, bool* tooLong) {
    wchar_t buf[kValueMax] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"", buf, kValueMax, ini);

    *tooLong = n >= kValueMax - 1;  // the API returns nSize - 1 when it truncated

    return std::wstring(buf, n);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* ini, int hi = 1) {
    wchar_t buf[kValueMax] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, kValueMax, ini);
    if (n == 1 && buf[0] == 1) {
        return def;
    }

    wchar_t* end = nullptr;

    errno = 0;
    const long v = std::wcstol(buf, &end, 10);

    while (std::iswspace(*end)) {
        ++end;
    }

    if (n >= kValueMax - 1 || end == buf || !n || *end || errno == ERANGE || v < 0 || v > hi) {
        Rs7Log("config: [%ls] %ls invalid integer; disabled", section, key);
        return 0;
    }

    return int(v);
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
    return ReadInt(section, key, def, iniPath, hi);
}

std::wstring Rs7Config_ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, bool* tooLong) {
    return ReadString(section, key, iniPath, tooLong);
}

bool Rs7Config_ReadPresent(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, std::wstring* out) {
    wchar_t buf[kValueMax] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, kValueMax, iniPath);
    if (n == 1 && buf[0] == 1) {
        return false;
    }

    *out = std::wstring(buf, n);

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
