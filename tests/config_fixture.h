#pragma once
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "platform/config.h"
#include "platform/settings.h"
#include "settings_removed.h"

// Every feature's startup read at once, as Rsxr_Startup runs them (RsxrSet_ReadIni), with the fields under their
// feature-struct names, for the config tests.
struct RsxrTestConfig : RsxrDiagConfig, RsxrColourConfig, RsxrString7Config, RsxrTunerConfig {
    bool found = false;      // the ini file exists
    int removedKeys = 0;     // keys of kRsxrRemovedKeys present in the file
    bool songEndLog = true;  // [debug] song_end_log
};

inline RsxrTestConfig RsxrTest_LoadConfig(const wchar_t* iniPath) {
    RsxrTestConfig c;

    c.found = RsxrSet_ReadIni(iniPath);
    static_cast<RsxrDiagConfig&>(c) = RsxrDiag_Config();
    static_cast<RsxrColourConfig&>(c) = RsxrColour_Config();
    static_cast<RsxrString7Config&>(c) = RsxrString7_Config();
    static_cast<RsxrTunerConfig&>(c) = RsxrTuner_Config();
    c.removedKeys = RsxrRemoved_Count();
    c.songEndLog = RsxrSongEnd_Config();

    return c;
}
