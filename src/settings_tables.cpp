// Every settings table, listed once (platform/settings.h). Each feature owns its table (keys, defaults, overlay
// labels, help, its startup read); this list only orders them.
//
// The order is the order of the keys (Rs7Set_Def, Rs7Set_Save) and of the startup reads, and within each ini section
// the order of the release template (scripts/package.ps1 orders the sections, docs/release/SETTINGS.md follows it):
//   [debug]  diagnostics, tuner_autotune, song_end_log, head_*      -> diag, tuner, song end, headstock
//   [neck]   string7_line, string7_v, fret_*, headstock_peg, eight  -> string7, frets, headstock, eight
// kRs7RemovedSettings (no keys) follows diag: the removed keys are logged right after [debug] diagnostics is read.
//
// kRs7UiTables lists every feature's overlay part once (platform/uilayout.h). Its order is the order inside each tab:
//   Status "features"  ramp copy, 7th string line, fret extend, 7th tuning peg, tuner, diagnostics
//   Neck  string7, frets, headstock    Debug  diag, song end    Window  window, overlay
//   Player (the default tabs)  colour, string7 ("Guitar", with frets and headstock below it), tuner, overlay
#include "features/chords/chords.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/psarc/psarc.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/tuner/tuner.h"
#include "features/window/window.h"
#include "model/eight.h"
#include "overlay/overlay.h"
#include "platform/settings.h"
#include "platform/uilayout.h"
#include "settings_removed.h"

const Rs7SetTable* const kRs7SettingTables[] = {
    &kRs7OverlaySettings, &kRs7DiagSettings,  &kRs7RemovedSettings,   &kRs7TunerSettings,  &kRs7SongEndSettings,
    &kRs7String7Settings, &kRs7FretSettings,  &kRs7HeadstockSettings, &kRs7ColourSettings, &kRs7ChordSettings,
    &kRs7EightSettings,   &kRs7PsarcSettings, &kRs7WindowSettings,
};
const int kRs7SettingTableCount = int(sizeof kRs7SettingTables / sizeof kRs7SettingTables[0]);

const Rs7UiTable* const kRs7UiTables[] = {
    &kRs7ColourUi, &kRs7String7Ui, &kRs7FretUi,   &kRs7HeadstockUi, &kRs7TunerUi,
    &kRs7DiagUi,   &kRs7SongEndUi, &kRs7WindowUi, &kRs7OverlayUi,
};
const int kRs7UiTableCount = int(sizeof kRs7UiTables / sizeof kRs7UiTables[0]);
