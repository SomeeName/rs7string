#pragma once
#include <cstdint>

// The keys removed in 1.0 (listed in settings_tables.cpp like a feature's settings table).

struct RsxrSetTable;
extern const RsxrSetTable kRsxrRemovedSettings;  // no rows: its read logs every removed key present

// Keys of the legacy 7-string paths removed in 1.0. An old rsxr.ini that still has them loads: each one present is
// logged as "ignored, removed in 1.0" and changes nothing.
struct RsxrRemovedKey {
    const wchar_t* section;
    const wchar_t* key;
};

extern const RsxrRemovedKey kRsxrRemovedKeys[];
extern const int kRsxrRemovedKeyCount;

// Logs each removed key present in a resolved ini path (nullptr: none) and returns their number.
int RsxrRemoved_ReadIni(const wchar_t* iniPath);
// The last startup read (RsxrSet_ReadIni).
int RsxrRemoved_Count();
