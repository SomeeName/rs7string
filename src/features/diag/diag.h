#pragma once
#include <cstdint>

// Diagnostic log points D1-D5 (docs/ARCHITECTURE.md, "Diagnostic log points").
// They run inside game render paths, so:
//  - each (point, string) pair logs only its first kRsxrDiagMaxHits hits per song (reset by
//    Rsxr_OnSongTuning); only strings 0 (comparison) and 6 are logged;
//  - the loggers are integer-only: floats are passed and printed as raw IEEE bits (%08X), so no
//    x87/SSE float code runs while the game holds values on the FPU stack;
//  - game memory is read through an SEH-guarded copy (access violations only), and only while the
//    (point, string) budget has hits left (RsxrDiag_Peek before, RsxrDiag_Take after the reads);
//  - nothing may throw out of them.
//  - With rsxr.ini [debug] diagnostics=0 (default) none of them is called (g_rsxr_diag).
enum RsxrDiagPoint : int { kRsxrD1 = 0, kRsxrD2, kRsxrD3, kRsxrD4, kRsxrD5, kRsxrDiagPointCount };

constexpr int kRsxrDiagMaxHits = 8;

// True if this hit of (point, string) may be logged (counts it). Strings other than 0 and 6: false.
bool RsxrDiag_Take(int point, uint32_t string);
// True if (point, string) has budget left; counts nothing. The loggers peek before reading game
// memory (no SEH-guarded reads once the budget is spent) and take only after the reads succeeded.
bool RsxrDiag_Peek(int point, uint32_t string);
// New song: every (point, string) gets kRsxrDiagMaxHits hits again.
void RsxrDiag_Reset();
// Logs the D1 lines Rsxr_DiagNoteHead queued (call from a full-mode context: it formats with the CRT). The full-mode
// diagnostic hooks D2 D3 D5 and the always-installed full-mode N5 frame hook call it.
void RsxrDiag_FlushNoteHeads();

// Called from the asm stubs and R2 (cdecl). Byte arguments arrive as dwords; only the low byte counts.
extern "C" {
// D1 (in the R2 mid-hook, 0x79E726, sse mode): note head lane. row = cell row of the fret (string 0's cell z at
// +0x44, string 1's at +0x90); z = the lane z the hook returns; absCz = |z - floor|. Integer only: it queues the line
// for RsxrDiag_FlushNoteHeads. Exported: the emulator oracle replaces it.
__declspec(dllexport) void __cdecl Rsxr_DiagNoteHead(uint32_t string, uint32_t fret, uint32_t open, const uint8_t* row,
                                                     uint32_t floorBits, uint32_t zBits, uint32_t absCzBits);
// D2 (0x7A22E0 Rnd_Gem_Create entry): every gem created for a string-0/6 note.
void __cdecl Rsxr_DiagGemCreate(uint32_t caller, uint32_t model, uint32_t note, uint32_t fret, uint32_t string,
                                uint32_t type);
// D3 (0x7A90F5 Rnd_GemGeom_Build mode switch): geom +0x8 gem, +0x14 mode, vec3s at +0x20/+0x2C/+0x38.
void __cdecl Rsxr_DiagGeomBuild(const uint8_t* geom);
// D4 (in the R1 stub, 0x58FE00 Rnd_FretGrid_GetCellPos). String 6: out = the stub's result.
// String 0 (out == nullptr): the stock result, read from the grid before the stock code runs.
void __cdecl Rsxr_DiagCellPos(uint32_t caller, const uint8_t* grid, uint32_t fret, uint32_t k, uint32_t string,
                              const uint32_t* out);
// D5 (0x7A4E5C Rnd_NoteGems_UpdatePositions join): gem +0xC string, +0xD fret, +0x10 type,
// +0x14 offset vec3; pos = the pre-offset position vec3 ([esp+1Ch] at the site).
__declspec(dllexport) void __cdecl Rsxr_DiagGemPos(const uint8_t* gem, const uint32_t* pos);
}

// [debug] diagnostics=0|1  1: write the diagnostic sites D2/D3/D5 and let R2/R1 call the D1/D4 loggers (rate-limited
// lines in rsxr.log). 0 (default): D2/D3/D5 are still resolved (a moved pattern still patches nothing) but not
// written, and R2/R1 skip the logger calls. The switch is g_rsxr_diag, set by Rsxr_Startup before any site is patched.
extern "C" __declspec(dllexport) uintptr_t g_rsxr_diag;
struct RsxrHookTable;
extern const RsxrHookTable kRsxrDiagHooks;  // D2 D3 D5, group Diag
struct RsxrSetTable;
extern const RsxrSetTable kRsxrDiagSettings;
// The diagnostics status row and the Debug tab's section (platform/uilayout.h).
struct RsxrUiTable;
extern const RsxrUiTable kRsxrDiagUi;

struct RsxrDiagConfig {
    int diagnosticsRaw = 0;    // the value as read
    bool diagnostics = false;  // raw != 0
};

// Reads [debug] diagnostics from a resolved ini path (nullptr: the default).
RsxrDiagConfig RsxrDiag_ReadIni(const wchar_t* iniPath);
// The last startup read (RsxrSet_ReadIni).
const RsxrDiagConfig& RsxrDiag_Config();
