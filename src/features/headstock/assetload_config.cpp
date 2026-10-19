#include "features/headstock/assetload_config.h"
#include "features/headstock/assetload.h"
#include "platform/config.h"
#include "platform/log.h"
#include <windows.h>
#include <cmath>
#include <cwchar>
#include <cwctype>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_asset_on = 0;
}

namespace rs7asset {
std::wstring g_dir;
std::wstring g_suffix = kDefaultSuffix;
std::wstring g_file7;
std::wstring g_file7Lh;
bool g_sbWanted = false, g_sbWantedLh = false;
std::wstring g_sbIni, g_sbDllDir;
volatile long g_sbState = 0, g_sbStateLh = 0;
float g_wood = kDefaultWood;
}  // namespace rs7asset

using namespace rs7asset;

namespace {
// [assets] headstock7_wood: a number in (0.05, 1], else the default.
float WoodFactor(const wchar_t* text) {
    if (!text[0]) {
        return kDefaultWood;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    const bool parsed = end != text;

    while (*end && std::iswspace(*end)) {
        ++end;
    }

    return parsed && !*end && std::isfinite(v) && v > 0.05 && v <= 1.0 ? float(v) : kDefaultWood;
}

// The folder (with its trailing separator) of a file path, "" without one.
std::wstring FolderOfPath(const wchar_t* path) {
    const std::wstring p = path ? path : L"";
    const size_t sep = p.find_last_of(L"\\/");

    return sep == std::wstring::npos ? std::wstring() : p.substr(0, sep + 1);
}

// The wood factor and the in-line right-hand frame settings, logged at startup.
void LogMainSettings(int on, bool exists, bool fileKey, bool woodSet) {
    Rs7Log("config: [assets] headstock7_wood=%.2f%s (brightness of the 7-string model's wood colour texture)",
           double(g_wood), woodSet ? "" : " (default)");
    Rs7Log("config: [assets] headstock7=%d headstock7_file=%ls%s dir=%ls suffix=%ls -> %s", on,
           g_file7.empty() ? L"(none)" : g_file7.c_str(),
           g_file7.empty() ? ""
           : exists        ? (fileKey ? "" : " (default)")
                           : " (MISSING: the edited-file rule only)",
           g_dir.c_str(), g_suffix.c_str(),
           on ? "7-string guitars get the 7-string headstock model (AL1 + N5): headstock7_file for the in-line "
                "right-hand frame, else <dir>\\<base><suffix>.nif"
              : "off (AL1 replays stock)");
}
}  // namespace

void Rs7Asset_Configure(const wchar_t* iniPath) {
    const int on = int(Rs7Config_ReadInt(L"assets", L"headstock7", 1, iniPath));
    // The built-in defaults are relative to the DLL's folder, which is the folder of rs7string.ini
    // (startup.cpp IniPath).
    const std::wstring dllDir = FolderOfPath(iniPath);
    const wchar_t* truncated = nullptr;
    auto readSetting = [&](const wchar_t* key, const wchar_t* fallback, wchar_t* out, DWORD capacity) {
        if (GetPrivateProfileStringW(L"assets", key, fallback, out, capacity, iniPath) >= capacity - 1) {
            truncated = key;  // exact-fit is indistinguishable from truncation: fail closed
        }
    };
    wchar_t buf[MAX_PATH] = {};

    readSetting(L"dir", kDefaultDir, buf, MAX_PATH);
    g_dir = Rs7Asset_ResolveDir(buf, dllDir);
    wchar_t suf[64] = {};

    readSetting(L"suffix", kDefaultSuffix, suf, 64);
    g_suffix = suf;

    for (wchar_t ch : g_suffix) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'.') {
            g_suffix = kDefaultSuffix;
        }
    }

    // headstock7_file / headstock7_file_lh: the resolved path (the default file when the key is absent).
    auto readModelFile = [&](const wchar_t* key, const wchar_t* defaultFile, bool& keySet) {
        wchar_t value[MAX_PATH] = {};

        readSetting(key, L"?", value, MAX_PATH);
        keySet = !(value[0] == L'?' && value[1] == 0);

        return Rs7Asset_ResolveFile(keySet ? std::wstring(value) : std::wstring(defaultFile), g_dir);
    };
    bool fileKey = false, fileKeyLh = false;

    g_file7 = readModelFile(L"headstock7_file", kDefaultFile7, fileKey);
    g_file7Lh = readModelFile(L"headstock7_file_lh", kDefaultFile7Lh, fileKeyLh);
    wchar_t wood[32] = {};

    readSetting(L"headstock7_wood", L"", wood, 32);

    if (truncated) {
        g_rs7_asset_on = 0;
        g_sbWanted = g_sbWantedLh = false;
        g_file7.clear();
        g_file7Lh.clear();
        Rs7Log("config: [assets] %ls truncated: headstock7 disabled; shorten the value", truncated);
        return;
    }

    g_wood = WoodFactor(wood);

    g_rs7_asset_on = on ? 1 : 0;
    const bool exists = !g_file7.empty() && GetFileAttributesW(g_file7.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool existsLh = !g_file7Lh.empty() && GetFileAttributesW(g_file7Lh.c_str()) != INVALID_FILE_ATTRIBUTES;

    LogMainSettings(on, exists, fileKey, wood[0] != 0);
    // Issue #28: no usable model file -> build one at the first guitar frame load (the game's VFS is up then).
    const bool sb = Rs7Config_ReadInt(L"assets", L"selfbuild", 1, iniPath) != 0;

    // headstock7_file[_lh]=none/off/0: the owner opted out. Left-handed in-line frame (epi_whilshire_cherry_lh.nif):
    // same rules, own key, own self-built file.
    g_sbWanted = on && !exists && !(fileKey && g_file7.empty()) && sb;
    g_sbWantedLh = on && !existsLh && !(fileKeyLh && g_file7Lh.empty()) && sb;

    if (g_sbWanted || g_sbWantedLh) {
        g_sbIni = iniPath ? iniPath : L"";
        g_sbDllDir = dllDir;
    }

    if (g_sbWanted) {
        Rs7Log("config: [assets] selfbuild=1 and no usable headstock7_file -> the 7-string model is built from the "
               "game's own "
               "assets at the first guitar load, into %ls",
               g_dir.c_str());
    } else if (sb) {
        Rs7Log("config: [assets] selfbuild=1 -> not needed (%s)", !on      ? "headstock7=0"
                                                                  : exists ? "headstock7_file exists"
                                                                           : "headstock7_file=none");
    }

    if (!exists) {
        g_file7.clear();
    }

    Rs7Log("config: [assets] headstock7_file_lh=%ls%s -> %s", g_file7Lh.empty() ? L"(none)" : g_file7Lh.c_str(),
           g_file7Lh.empty() ? ""
           : existsLh        ? (fileKeyLh ? "" : " (default)")
                             : " (missing)",
           !on            ? "off"
           : existsLh     ? "left-handed in-line frame gets this model"
           : g_sbWantedLh ? "selfbuild at the first left-handed guitar load"
                          : "left-handed frame stays stock");
    if (!existsLh) {
        g_file7Lh.clear();
    }
}
