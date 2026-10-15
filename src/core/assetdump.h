#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Asset dump (owner's request, rs7string.ini [debug] dump_assets=1): the game's own PSARC reader
// opens every .psarc in the game folder (it decrypts the TOC and inflates the entries itself; our
// code never touches key data) and we write each entry to <dump_dir>\<archive>\<entry path>.
// Default dump_dir: %USERPROFILE%\Desktop\rocksmith-assets. Archives with a ".done" marker are
// skipped, so an interrupted dump resumes. Runs on its own thread at below-normal priority once
// the game's file system is up. Research: docs/ARCHITECTURE.md.
void Rs7AssetDump_Start(const wchar_t* iniPath, const wchar_t* gameDir);
// The overlay's "dump now" (o dump): the same dump without the 30 s menu wait, whatever dump_assets
// says. One dump per run: false when one was already started.
bool Rs7AssetDump_Trigger(const wchar_t* iniPath, const wchar_t* gameDir);

// state: 0 idle, 1 waiting for the game's file system, 2 running, 3 done, 4 failed.
struct Rs7AssetDumpStatus {
    int state = 0, archivesDone = 0, archives = 0, files = 0;
};

Rs7AssetDumpStatus Rs7AssetDump_Status();

// Exposed for tests.
// Entry name -> relative output path ('/' -> '\'); empty when the name is unsafe (absolute,
// drive letter, "..", control characters) or empty.
std::wstring Rs7AssetDump_SafeRelPath(const char* name);
// Sort key: guitar assets first, then small asset archives, then audio/video/songs/dlc.
int Rs7AssetDump_Priority(const std::wstring& relArchivePath);
// The worker exception boundary; cleanup is called exactly once even if work faults.
// Exposed so tests can inject failures without opening game resources.
bool Rs7AssetDump_RunWorker(void (*work)(void*), void* context, void (*cleanup)(void*));

// Dormant self-build support: caller must wait for VFS initialization. Reads only
// the two headstock templates using the same September-2022 game reader as the dump.
// Unknown executable, unready VFS, read fault or oversized entry -> false, empty outputs.
bool Rs7AssetDump_ReadHeadstockTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool Rs7AssetDump_ReadHeadstockTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
// The in-line peg (epi_whilshire_peg.nif) and its wilshire_hardware.dds, read with the game's own reader.
bool Rs7AssetDump_ReadInlinePeg(std::vector<uint8_t>& peg, std::vector<uint8_t>& hardware);
