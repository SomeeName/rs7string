#pragma once
#include <cstdint>
#include <string>

// Reading the settings file (next to the DLL): the helpers every feature's own ini read uses. No key lives here: a
// feature's keys, defaults and help text are its settings table (features/<name>/settings.cpp, platform/settings.h),
// its typed startup read sits next to that table. A missing file or key means the feature's default.

// The settings file next to the DLL. dir ends with a separator; a missing file means every default.
constexpr wchar_t kRsxrIniName[] = L"rsxr.ini";
std::wstring RsxrConfig_FindIni(const std::wstring& dir);

// Profile APIs resolve bare filenames under Windows, while disk APIs use the current directory.
// Normalize once so all readers address the same file. Empty means invalid/overlong input.
std::wstring RsxrConfig_ResolveIniPath(const wchar_t* iniPath);

// The longest ini value any reader accepts, in characters (GetPrivateProfileString strips surrounding blanks). The
// settings store's Text buffer (kRsxrSetTextMax, platform/settings.h) holds this many plus the terminator.
constexpr int kRsxrIniValueMax = 259;

// Missing keys use def; malformed/too long/out-of-range values return zero and log.
int RsxrConfig_ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* iniPath, int hi = 1);

// The text of a key ("" = missing or empty). A value longer than kRsxrIniValueMax is not cut short (a truncated
// "C8C8C8   junk" would parse): *tooLong is set and the caller treats the value as invalid.
std::wstring RsxrConfig_ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, bool* tooLong);

// For keys where a present but empty value is a choice: false when the key is missing, else *out = its text and, if
// tooLong is given, whether the value was longer than kRsxrIniValueMax (then *out is only a prefix).
bool RsxrConfig_ReadPresent(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, std::wstring* out,
                            bool* tooLong = nullptr);

// "C8C8C8", "#c8c8c8", surrounding blanks/tabs ok; exactly six hex digits. *rgb untouched on failure.
bool Rsxr_ParseRgb(const wchar_t* text, uint32_t* rgb);
