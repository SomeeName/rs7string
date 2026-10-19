#pragma once
#include <cstdint>
#include <string>

// Reading rs7string.ini (next to the DLL): the helpers every feature's own ini read uses. No key lives here: a
// feature's keys, defaults and help text are its settings table (features/<name>/settings.cpp, platform/settings.h),
// its typed startup read sits next to that table. A missing file or key means the feature's default.

// Profile APIs resolve bare filenames under Windows, while disk APIs use the current directory.
// Normalize once so all readers address the same file. Empty means invalid/overlong input.
std::wstring Rs7Config_ResolveIniPath(const wchar_t* iniPath);

// Missing keys use def; malformed/truncated/out-of-range values return zero and log.
int Rs7Config_ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* iniPath, int hi = 1);

// The text of a key ("" = missing or empty; GetPrivateProfileString strips surrounding blanks). A value of 255 or
// more characters is not cut short (a truncated "C8C8C8   junk" would parse): *tooLong is set and the caller treats
// the value as invalid.
std::wstring Rs7Config_ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, bool* tooLong);

// For keys where a present but empty value is a choice: false when the key is missing, else *out = its text.
bool Rs7Config_ReadPresent(const wchar_t* section, const wchar_t* key, const wchar_t* iniPath, std::wstring* out);

// "C8C8C8", "#c8c8c8", surrounding blanks/tabs ok; exactly six hex digits. *rgb untouched on failure.
bool Rs7_ParseRgb(const wchar_t* text, uint32_t* rgb);
