#include "features/headstock/assetload_config.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_tex.h"
#include "features/headstock/headstock.h"
#include "features/headstock/hsbuild_io.h"
#include "features/headstock/state.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/settings.h"
#include <windows.h>
#include <cwchar>
#include <filesystem>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_asset_on = 0;
}

rs7asset::ConfigState rs7asset::g_config{};

using namespace rs7asset;
using rs7head::g_head;

namespace {
// [assets] headstock7_wood: a number in the settings row's range (0.05 .. 1), else the default.
float WoodFactor(const wchar_t* text) {
    float v = kDefaultWood;

    return Rs7Set_ParseTableFloat(kRs7HeadstockSettings, "assets.headstock7_wood", text, &v) ? v : kDefaultWood;
}

// The folder (with its trailing separator) of a file path, "" without one.
std::wstring FolderOfPath(const wchar_t* path) {
    const std::wstring p = path ? path : L"";
    const size_t sep = p.find_last_of(L"\\/");

    return sep == std::wstring::npos ? std::wstring() : p.substr(0, sep + 1);
}

// Explicit defaults in shipped/older INIs are still generated files. Other paths remain custom, even aliases.
bool IsGeneratedModel(const std::wstring& path, const wchar_t* defaultFile) {
    const auto generated = std::filesystem::path(Rs7Asset_ResolveFile(defaultFile, g_config.dir)).lexically_normal();
    const auto chosen = std::filesystem::path(path).lexically_normal();

    return !path.empty() && _wcsicmp(chosen.c_str(), generated.c_str()) == 0;
}

// The wood factor and the in-line right-hand frame settings, logged at startup.
void LogMainSettings(int on, bool exists, bool fileKey, bool woodSet) {
    Rs7Log("config: [assets] headstock7_wood=%.2f%s (brightness of the 7-string model's wood colour texture)",
           double(g_head.wood), woodSet ? "" : " (default)");
    Rs7Log("config: [assets] headstock7=%d %ls=%ls%s dir=%ls suffix=%ls -> %s", on, ModelFilesFor(true).key,
           g_config.file7.empty() ? L"(none)" : g_config.file7.c_str(),
           g_config.file7.empty() ? ""
           : exists               ? (fileKey ? "" : " (default)")
                                  : " (MISSING: the edited-file rule only)",
           g_config.dir.c_str(), g_config.suffix.c_str(),
           on ? "7-string guitars get the 7-string headstock model (AL1 + N5): headstock7_file for the in-line "
                "right-hand frame, else <dir>\\<base><suffix>.nif"
              : "off (AL1 replays stock)");
}

// Which models are self-built. headstock7_file[_lh]=none/off/0: the owner opted out (the caller masks that).
// Left-handed in-line frame (epi_whilshire_cherry_lh.nif): same rules, own key, own self-built file. A default model
// built from an older package (an update shipped a new one) is rebuilt like a missing one; checkDefault/checkDefaultLh
// identify defaults to check. A custom headstock7_file is never replaced.
void PlanSelfBuild(bool on, bool sb, const std::wstring& dllDir, bool checkDefault, bool checkDefaultLh, bool exists,
                   bool existsLh) {
    const auto stale = [&](Rs7HsKit kit) {
        return on && sb && Rs7Asset_PackageChanged(dllDir.c_str(), g_config.dir.c_str(), kit, g_config.sbProtected);
    };
    const bool sharedStale = stale(Rs7HsKit::Guitar);
    const bool bassStale = stale(Rs7HsKit::Bass);

    g_head.sbWanted = on && sb && checkDefault && (!exists || sharedStale);
    g_head.sbWantedLh = on && sb && checkDefaultLh && (!existsLh || sharedStale || stale(Rs7HsKit::GuitarLh));
    g_head.sbBass = on && sb;  // the bass models: built when the first bass frame finds none (assetload_load.cpp)
    g_head.sbBassStale = sharedStale || bassStale;
    g_head.sbBassStaleLh = sharedStale || bassStale || stale(Rs7HsKit::BassLh);

    if (g_head.sbWanted || g_head.sbWantedLh || g_head.sbBass) {
        g_config.sbDllDir = dllDir;
    }
}

// Whether the model is self-built, logged at startup.
void LogSelfBuild(int on, bool exists, bool sb) {
    if (g_head.sbWanted) {
        Rs7Log("config: [assets] selfbuild=1 and no usable or an outdated headstock7_file -> the 7-string model is "
               "built from the game's own assets at the first guitar load, into %ls",
               g_config.dir.c_str());
    } else if (sb) {
        Rs7Log("config: [assets] selfbuild=1 -> not needed (%s)", !on      ? "headstock7=0"
                                                                  : exists ? "headstock7_file exists"
                                                                           : "headstock7_file=none");
    }
}

// The left-handed in-line frame's model file, logged at startup.
void LogLhSettings(int on, bool existsLh, bool fileKeyLh, const wchar_t* keyLh) {
    Rs7Log("config: [assets] %ls=%ls%s -> %s", keyLh, g_config.file7Lh.empty() ? L"(none)" : g_config.file7Lh.c_str(),
           g_config.file7Lh.empty() ? ""
           : existsLh               ? (fileKeyLh ? "" : " (default)")
                                    : " (missing)",
           !on                 ? "off"
           : existsLh          ? "left-handed in-line frame gets this model"
           : g_head.sbWantedLh ? "selfbuild at the first left-handed guitar load"
                               : "left-handed frame stays stock");
}
}  // namespace

void Rs7Asset_Configure(const wchar_t* iniPath) {
    g_config.sbProtected.clear();
    const int on = Rs7Set_ReadTableInt(kRs7HeadstockSettings, "assets.headstock7", iniPath);
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
    g_config.dir = Rs7Asset_ResolveDir(buf, dllDir);
    wchar_t suf[64] = {};

    readSetting(L"suffix", kDefaultSuffix, suf, 64);
    g_config.suffix = suf;

    for (wchar_t ch : g_config.suffix) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'.') {
            g_config.suffix = kDefaultSuffix;
        }
    }

    // headstock7_file / headstock7_file_lh: the resolved path (the default file when the key is absent).
    auto readModelFile = [&](const wchar_t* key, const wchar_t* defaultFile, bool& keySet) {
        wchar_t value[MAX_PATH] = {};

        readSetting(key, L"?", value, MAX_PATH);
        keySet = !(value[0] == L'?' && value[1] == 0);
        const auto path = Rs7Asset_ResolveFile(keySet ? std::wstring(value) : std::wstring(defaultFile), g_config.dir);

        keySet = keySet && !IsGeneratedModel(path, defaultFile);
        if (keySet && !path.empty()) {
            g_config.sbProtected.push_back(path);
        }

        return path;
    };
    bool fileKey = false, fileKeyLh = false;

    // Eight strings are always on: the 8-peg model under its own keys, the 7-peg one next to it.
    const ModelFiles model = ModelFilesFor(true);

    g_config.file7 = readModelFile(model.key, model.file, fileKey);
    g_config.file7Lh = readModelFile(model.keyLh, model.fileLh, fileKeyLh);
    g_head.modelPegs[0] = g_head.modelPegs[1] = long(model.pegs);

    const ModelFiles seven = ModelFilesFor(false);  // the 7-peg model as the frame's second model
    bool unused = false;

    g_config.file7s = readModelFile(seven.key, seven.file, unused);
    g_config.file7sLh = readModelFile(seven.keyLh, seven.fileLh, unused);

    wchar_t wood[32] = {};

    readSetting(L"headstock7_wood", L"", wood, 32);

    if (truncated) {
        g_rs7_asset_on = 0;
        g_head.sbWanted = g_head.sbWantedLh = false;
        g_config.file7.clear();
        g_config.file7Lh.clear();
        g_config.file7s.clear();
        g_config.file7sLh.clear();
        Rs7Log("config: [assets] %ls truncated: headstock7 disabled; shorten the value", truncated);
        return;
    }

    g_head.wood = WoodFactor(wood);

    g_rs7_asset_on = on ? 1 : 0;
    const bool exists = FilePresent(g_config.file7);
    const bool existsLh = FilePresent(g_config.file7Lh);

    LogMainSettings(on, exists, fileKey, wood[0] != 0);
    // Issue #28: no usable model file -> build one at the first guitar frame load (the game's VFS is up then).
    const bool sb = Rs7Config_ReadInt(L"assets", L"selfbuild", 1, iniPath) != 0;

    PlanSelfBuild(on != 0, sb, dllDir, !fileKey, !fileKeyLh, exists, existsLh);
    g_head.sbWanted = g_head.sbWanted && !(fileKey && g_config.file7.empty());
    g_head.sbWantedLh = g_head.sbWantedLh && !(fileKeyLh && g_config.file7Lh.empty());

    LogSelfBuild(on, exists, sb);

    if (!exists) {
        g_config.file7.clear();
    }

    LogLhSettings(on, existsLh, fileKeyLh, model.keyLh);

    if (!existsLh) {
        g_config.file7Lh.clear();
    }
}
