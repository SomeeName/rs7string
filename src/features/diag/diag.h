#pragma once
#include <cstdint>

// Diagnostic log points D1-D5 of docs/ARCHITECTURE.md ("Diagnostic log points").
// They run inside game render paths, so:
//  - each (point, string) pair logs only its first kRs7DiagMaxHits hits per song (reset by
//    Rs7_OnSongTuning); only strings 0 (comparison) and 6 are logged;
//  - the loggers are integer-only: floats are passed and printed as raw IEEE bits (%08X), so no
//    x87/SSE float code runs while the game holds values on the FPU stack;
//  - game memory is read through an SEH-guarded copy (access violations only), and only while the
//    (point, string) budget has hits left (Rs7Diag_Peek before, Rs7Diag_Take after the reads);
//  - nothing may throw out of them.
//  - With rs7string.ini [debug] diagnostics=0 (default) none of them is called (g_rs7_diag).
enum Rs7DiagPoint : int { kRs7D1 = 0, kRs7D2, kRs7D3, kRs7D4, kRs7D5, kRs7DiagPointCount };

constexpr int kRs7DiagMaxHits = 8;

// True if this hit of (point, string) may be logged (counts it). Strings other than 0 and 6: false.
bool Rs7Diag_Take(int point, uint32_t string);
// True if (point, string) has budget left; counts nothing. The loggers peek before reading game
// memory (no SEH-guarded reads once the budget is spent) and take only after the reads succeeded.
bool Rs7Diag_Peek(int point, uint32_t string);
// New song: every (point, string) gets kRs7DiagMaxHits hits again.
void Rs7Diag_Reset();
// Logs the D1 lines Rs7_DiagNoteHead queued (call from a full-mode context: it formats with the CRT). The full-mode
// diagnostic hooks D2 D3 D5 call it; they are written under the same switch as D1.
void Rs7Diag_FlushNoteHeads();

// Called from the asm stubs and R2 (cdecl). Byte arguments arrive as dwords; only the low byte counts.
extern "C" {
// D1 (in the R2 mid-hook, 0x79E726, sse mode): note head lane. row = cell row of the fret (string 0's cell z at
// +0x44, string 1's at +0x90); z = the lane z the hook returns; absCz = |z - floor|. Integer only: it queues the line
// for Rs7Diag_FlushNoteHeads. Exported: the emulator oracle replaces it.
__declspec(dllexport) void __cdecl Rs7_DiagNoteHead(uint32_t string, uint32_t fret, uint32_t open, const uint8_t* row,
                                                    uint32_t floorBits, uint32_t zBits, uint32_t absCzBits);
// D2 (0x7A22E0 Rnd_Gem_Create entry): every gem created for a string-0/6 note.
void __cdecl Rs7_DiagGemCreate(uint32_t caller, uint32_t model, uint32_t note, uint32_t fret, uint32_t string,
                               uint32_t type);
// D3 (0x7A90F5 Rnd_GemGeom_Build mode switch): geom +0x8 gem, +0x14 mode, vec3s at +0x20/+0x2C/+0x38.
void __cdecl Rs7_DiagGeomBuild(const uint8_t* geom);
// D4 (in the R1 stub, 0x58FE00 Rnd_FretGrid_GetCellPos). String 6: out = the stub's result.
// String 0 (out == nullptr): the stock result, read from the grid before the stock code runs.
void __cdecl Rs7_DiagCellPos(uint32_t caller, const uint8_t* grid, uint32_t fret, uint32_t k, uint32_t string,
                             const uint32_t* out);
// D5 (0x7A4E5C Rnd_NoteGems_UpdatePositions join): gem +0xC string, +0xD fret, +0x10 type,
// +0x14 offset vec3; pos = the pre-offset position vec3 ([esp+1Ch] at the site).
__declspec(dllexport) void __cdecl Rs7_DiagGemPos(const uint8_t* gem, const uint32_t* pos);
}

// [debug] diagnostics=0|1  1: write the diagnostic sites D2/D3/D5 and let R2/R1 call the D1/D4 loggers (rate-limited
// lines in rs7string.log). 0 (default): D2/D3/D5 are still resolved (a moved pattern still patches nothing) but not
// written, and R2/R1 skip the logger calls. The switch is g_rs7_diag, set by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) uintptr_t g_rs7_diag;
struct Rs7HookTable;
extern const Rs7HookTable kRs7DiagHooks;  // D2 D3 D5, group Diag
struct Rs7SetTable;
extern const Rs7SetTable kRs7DiagSettings;
// The diagnostics status row and the Debug tab's section (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7DiagUi;

struct Rs7DiagConfig {
    int diagnosticsRaw = 0;    // the value as read
    bool diagnostics = false;  // raw != 0
};

// Reads [debug] diagnostics from a resolved ini path (nullptr: the default).
Rs7DiagConfig Rs7Diag_ReadIni(const wchar_t* iniPath);
// The last startup read (Rs7Set_ReadIni).
const Rs7DiagConfig& Rs7Diag_Config();
