#pragma once
#include <string>

// Leak audit support ([debug] alloc_stats=1, default 0; README [debug] alloc_stats). Every allocation type the
// DLL owns, enlarges or keeps alive carries a live/total counter (Interlocked, always counted: a few
// nanoseconds), and RsxrAlloc_Snapshot logs one line per call with every live count next to the
// process's private bytes, the free address space (the game is 32-bit) and its handle count. The
// snapshots are taken at song setup (pitch/hooks_mode.cpp, Rsxr_OnSongTuning) and when the song session leaves
// state 3 (songend.cpp); a run of several songs shows which "live" value grows per song.
enum RsxrAllocKind : int {
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

void RsxrAlloc_Configure(const wchar_t* iniPath);  // [debug] alloc_stats
void RsxrAlloc_SetEnabledForTests(bool on);
void RsxrAlloc_Inc(RsxrAllocKind kind);
void RsxrAlloc_Dec(RsxrAllocKind kind);
long RsxrAlloc_Live(RsxrAllocKind kind);   // Inc - Dec
long RsxrAlloc_Total(RsxrAllocKind kind);  // Inc
void RsxrAlloc_ResetForTests();
const char* RsxrAlloc_Name(RsxrAllocKind kind);
// Logs "allocstats <when> #<n>: ..." if enabled; a "song start" snapshot numbers the songs. Never throws.
void RsxrAlloc_Snapshot(const char* when);
// The line without the log (tests): gauges of other modules are read like the log does.
std::string RsxrAlloc_Line(const char* when, int song);
