// The song end log's setting: [debug] song_end_log and its startup read.
#include "platform/settings.h"
#include "features/songend/songend.h"
#include "platform/config.h"
#include "platform/uilayout.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

bool g_songEndLog = true;

void ApplyEndLog(const RsxrSetValue& v) {
    g_rsxr_endlog = v.i ? 1 : 0;
}

void ReadSongEnd(const wchar_t* iniPath) {
    g_songEndLog = RsxrSongEnd_ReadIni(iniPath);
}

const RsxrSetDef kSongEndDefs[] = {
    {"debug.song_end_log", L"debug", L"song_end_log", T::Bool, W::Live, "1", 0, 1, "Song end log",
     "Logs how each song ended (for bug reports).", ApplyEndLog},
};
}  // namespace

const RsxrSetTable kRsxrSongEndSettings = {kSongEndDefs, int(sizeof kSongEndDefs / sizeof kSongEndDefs[0]),
                                           ReadSongEnd};

namespace {
using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    {U::Debug, K::Setting, "debug.song_end_log"},
};
}  // namespace

const RsxrUiTable kRsxrSongEndUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

bool RsxrSongEnd_ReadIni(const wchar_t* iniPath) {
    return !iniPath || RsxrSet_ReadTableInt(kRsxrSongEndSettings, "debug.song_end_log", iniPath) != 0;
}

bool RsxrSongEnd_Config() {
    return g_songEndLog;
}
