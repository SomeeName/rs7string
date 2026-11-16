#pragma once
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "platform/config.h"
#include "platform/settings.h"
#include "settings_removed.h"

// Every feature's startup read at once, as Rs7_Startup runs them (Rs7Set_ReadIni), with the fields under their
// feature-struct names, for the config tests.
struct Rs7TestConfig : Rs7DiagConfig, Rs7ColourConfig, Rs7String7Config, Rs7TunerConfig {
    bool found = false;      // the ini file exists
    int removedKeys = 0;     // keys of kRs7RemovedKeys present in the file
    bool songEndLog = true;  // [debug] song_end_log
};

inline Rs7TestConfig Rs7Test_LoadConfig(const wchar_t* iniPath) {
    Rs7TestConfig c;

    c.found = Rs7Set_ReadIni(iniPath);
    static_cast<Rs7DiagConfig&>(c) = Rs7Diag_Config();
    static_cast<Rs7ColourConfig&>(c) = Rs7Colour_Config();
    static_cast<Rs7String7Config&>(c) = Rs7String7_Config();
    static_cast<Rs7TunerConfig&>(c) = Rs7Tuner_Config();
    c.removedKeys = Rs7Removed_Count();
    c.songEndLog = Rs7SongEnd_Config();

    return c;
}
