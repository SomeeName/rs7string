// Every settings table, listed once (platform/settings.h). Each feature owns its table (keys, defaults, overlay
// labels, help, its startup read); this list only orders them.
//
// The order is the order of the keys (RsxrSet_Def, RsxrSet_Save) and of the startup reads, and within each ini section
// the order of the release template (scripts/package.ps1 orders the sections, docs/release/SETTINGS.md follows it):
//   [debug]  diagnostics, tuner_autotune, song_end_log, head_*      -> diag, tuner, song end, headstock
//   [neck]   string7_line, spacing_*                         -> string7, spacing
// kRsxrRemovedSettings (no keys) follows diag: the removed keys are logged right after [debug] diagnostics is read.
//
// kRsxrUiTables lists every feature's overlay part once (platform/uilayout.h). Its order is the order inside each tab:
//   Status "features"  ramp copy, 7th string line, fret extend, 7th tuning peg, tuner, diagnostics
//   Neck  spacing, headstock    Debug  diag, song end    Window  window, overlay
//   Player (the default tabs)  colour, string7 ("Guitar", with headstock and chords below it; spacing is under Neck),
//   tuner, overlay
#include "features/chords/chords.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/menuload/menuload.h"
#include "features/psarc/psarc.h"
#include "features/songend/songend.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "features/window/window.h"
#include "overlay/overlay.h"
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "settings_removed.h"

const RsxrSetTable* const kRsxrSettingTables[] = {
    &kRsxrOverlaySettings, &kRsxrDiagSettings,    &kRsxrRemovedSettings,   &kRsxrTunerSettings,  &kRsxrSongEndSettings,
    &kRsxrString7Settings, &kRsxrSpacingSettings, &kRsxrHeadstockSettings, &kRsxrColourSettings, &kRsxrChordSettings,
    &kRsxrPsarcSettings,   &kRsxrWindowSettings,  &kRsxrMenuLoadSettings,
};
const int kRsxrSettingTableCount = int(sizeof kRsxrSettingTables / sizeof kRsxrSettingTables[0]);

const RsxrUiTable* const kRsxrUiTables[] = {
    &kRsxrColourUi, &kRsxrString7Ui, &kRsxrFretUi,    &kRsxrSpacingUi, &kRsxrHeadstockUi, &kRsxrChordUi,
    &kRsxrTunerUi,  &kRsxrDiagUi,    &kRsxrSongEndUi, &kRsxrWindowUi,  &kRsxrOverlayUi,
};
const int kRsxrUiTableCount = int(sizeof kRsxrUiTables / sizeof kRsxrUiTables[0]);
