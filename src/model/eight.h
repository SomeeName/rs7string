#pragma once
#include <cstdint>
// [neck] eight: preparation only; default off, startup only. Does not publish InstrumentInfo.
extern "C" __declspec(dllexport) uintptr_t g_rs7_eight;

struct Rs7SetTable;
// [neck] eight: its settings table and its startup read (eight.cpp).
extern const Rs7SetTable kRs7EightSettings;

struct Rs7EightConfig {
    int eightRaw = 0;
    bool eight = false;  // raw != 0
};

// Reads [neck] eight from a resolved ini path (nullptr: the default).
Rs7EightConfig Rs7Eight_ReadIni(const wchar_t* iniPath);
// The last startup read (Rs7Set_ReadIni).
const Rs7EightConfig& Rs7Eight_Config();
