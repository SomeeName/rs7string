// The song end log's setting: [debug] song_end_log and its startup read.
#include "platform/settings.h"
#include "features/songend/songend.h"
#include "platform/config.h"
#include "platform/uilayout.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

bool g_songEndLog = true;

void ApplyEndLog(const Rs7SetValue& v) {
    g_rs7_endlog = v.i ? 1 : 0;
}

void ReadSongEnd(const wchar_t* iniPath) {
    g_songEndLog = Rs7SongEnd_ReadIni(iniPath);
}

const Rs7SetDef kSongEndDefs[] = {
    {"debug.song_end_log", L"debug", L"song_end_log", T::Bool, W::Live, "1", 0, 1, "Song end log",
     "Log the song session's state changes and which route ends a song (SE1-SE3, issue #13).", ApplyEndLog},
};
}  // namespace

const Rs7SetTable kRs7SongEndSettings = {kSongEndDefs, int(sizeof kSongEndDefs / sizeof kSongEndDefs[0]), ReadSongEnd};

namespace {
using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    {U::Debug, K::Setting, "debug.song_end_log"},
};
}  // namespace

const Rs7UiTable kRs7SongEndUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

bool Rs7SongEnd_ReadIni(const wchar_t* iniPath) {
    return !iniPath || Rs7Config_ReadInt(L"debug", L"song_end_log", 1, iniPath) != 0;
}

bool Rs7SongEnd_Config() {
    return g_songEndLog;
}
