#pragma once
#include <string>

// Leak audit support ([debug] alloc_stats=1, default 0; README [debug] alloc_stats). Every allocation type the
// DLL owns, enlarges or keeps alive carries a live/total counter (Interlocked, always counted: a few
// nanoseconds), and Rs7Alloc_Snapshot logs one line per call with every live count next to the
// process's private bytes, the free address space (the game is 32-bit) and its handle count. The
// snapshots are taken at song setup (mode.cpp, Rs7_OnSongTuning) and when the song session leaves
// state 3 (songend.cpp); a run of several songs shows which "live" value grows per song.
enum Rs7AllocKind : int {
    kAkGuitarStrings,  // native GuitarStrings (kGsSize) - GSC1 .. GSFR / GSZ1
    kAkStringObjs,     // the 4 extra 0x110 string objects per native GuitarStrings (strings 6/7, main + ghost)
    kAkInfos,          // native InstrumentInfos (registry) - GIA1 .. GIZ1
    kAkNsT8,           // T8 chord template arrays (process heap)
    kAkNsC8,           // C8 ChordNotes arrays (process heap)
    kAkDiagFiles,      // diagram movie IFile replacements (Scaleform may retain them)
    kAkHeadSlots,      // per-guitar headstock-model state slots (assetload g_heads)
    kAkAssetMerges,    // edited headstock models merged (resource kept for the life of the game)
    kAkAssetTextures,  // 7-string headstock textures registered (kept)
    kAkAssetBinds,     // BindAlt runs: param holders + stock holder refs held (not released: see audit)
    kAkCount
};

void Rs7Alloc_Configure(const wchar_t* iniPath);  // [debug] alloc_stats
bool Rs7Alloc_Enabled();
void Rs7Alloc_SetEnabledForTests(bool on);
void Rs7Alloc_Inc(Rs7AllocKind kind);
void Rs7Alloc_Dec(Rs7AllocKind kind);
long Rs7Alloc_Live(Rs7AllocKind kind);   // Inc - Dec
long Rs7Alloc_Total(Rs7AllocKind kind);  // Inc
void Rs7Alloc_ResetForTests();
const char* Rs7Alloc_Name(Rs7AllocKind kind);
// Logs "allocstats <when> #<n>: ..." if enabled; a "song start" snapshot numbers the songs. Never throws.
void Rs7Alloc_Snapshot(const char* when);
// The line without the log (tests): gauges of other modules are read like the log does.
std::string Rs7Alloc_Line(const char* when, int song);
