#pragma once
// Temporary files of the native tests.
#include <windows.h>
#include <cstdio>
#include <string>

namespace rs7test {
// %TEMP%\rs7string_test_<tag>_<pid>.ini with `content`, for a test that loads an ini file. Pid-unique (parallel test
// runs do not collide); one file per tag, each call overwrites it.
inline std::wstring TempIni(const wchar_t* tag, const char* content) {
    wchar_t dir[MAX_PATH];

    GetTempPathW(MAX_PATH, dir);
    const std::wstring path =
        std::wstring(dir) + L"rs7string_test_" + tag + L"_" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    FILE* f = nullptr;

    _wfopen_s(&f, path.c_str(), L"wb");

    if (f) {
        std::fputs(content, f);
        std::fclose(f);
    }

    return path;
}
}  // namespace rs7test
