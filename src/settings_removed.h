#pragma once
#include <cstdint>

// The keys removed in 1.0 (listed in settings_tables.cpp like a feature's settings table).

struct Rs7SetTable;
extern const Rs7SetTable kRs7RemovedSettings;  // no rows: its read logs every removed key present

// Keys of the legacy 7-string paths removed in 1.0. An old rs7string.ini that still has them loads: each one present is
// logged as "ignored, removed in 1.0" and changes nothing.
struct Rs7RemovedKey {
    const wchar_t* section;
    const wchar_t* key;
};

extern const Rs7RemovedKey kRs7RemovedKeys[];
extern const int kRs7RemovedKeyCount;

// Logs each removed key present in a resolved ini path (nullptr: none) and returns their number.
int Rs7Removed_ReadIni(const wchar_t* iniPath);
// The last startup read (Rs7Set_ReadIni).
int Rs7Removed_Count();
