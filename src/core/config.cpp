#include "core/config.h"
#include "core/frets.h"
#include "log.h"
#include <windows.h>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace {
// Long enough for any sensible value; a longer one is not cut short (a truncated "C8C8C8   junk"
// would parse): *tooLong is set and the caller treats the value as invalid.
constexpr DWORD kValueMax = 256;

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* ini, bool* tooLong) {
    wchar_t buf[kValueMax] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"", buf, kValueMax, ini);

    *tooLong = n >= kValueMax - 1;  // the API returns nSize - 1 when it truncated

    return std::wstring(buf, n);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* ini, int hi = 1) {
    wchar_t buf[kValueMax] = {};
    const DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, kValueMax, ini);
    if (n == 1 && buf[0] == 1) {
        return def;
    }

    wchar_t* end = nullptr;

    errno = 0;
    const long v = std::wcstol(buf, &end, 10);

    while (std::iswspace(*end)) {
        ++end;
    }

    if (n >= kValueMax - 1 || end == buf || !n || *end || errno == ERANGE || v < 0 || v > hi) {
        Rs7Log("config: [%ls] %ls invalid integer; disabled", section, key);
        return 0;
    }

    return int(v);
}
}  // namespace

const Rs7RemovedKey kRs7RemovedKeys[] = {
    {L"debug", L"string6_as_string0"},  // diagnostic mirror of string 0 (lanes and colours)
    {L"neck", L"native_colours"},       // 0: the stock six-column colours with C1-C5 / N11 / GSCL
    {L"neck", L"native_tuner"},         // 0: the six-slot tuner with the low B as a phase of slot 0
    {L"tuner", L"seven_slot"},          // 0: the stock six-string tuner (also turned the native tuner off)
    {L"debug", L"tuner_trace"},         // logged the B-phase tuner's string selections
    {L"neck", L"native_pegset"},        // 0: the stage-2 7th peg mirrored from peg 1 (PG1-PG4, PH1/PH2)
    {L"neck", L"peg7_k"},               // that peg's spacing factor
    {L"neck", L"peg7_offset"},          // that peg's position offset
    {L"neck", L"native_strings"},       // 0: the DLL-owned 7th string object S7 / S7G (N2-N4, N6, N7, N14-N17)
    {L"neck", L"string7_draw_fix"},     // N10 added S7's mesh to the noteway draw list
    {L"debug", L"string7_gpu_diag"},    // observed S7's draw calls on the device (d3d/gpudiag)
    {L"neck", L"native_nextgrid"},      // 0: the stock six-row grid with R7b / N9 for string 6
    {L"neck", L"native_fretgrid"},      // 0: the stock six-string FretGrid
    {L"neck", L"native_pitch"},         // 0: the stock detector / matcher storage with the string-6 side store
    {L"neck", L"native_publish"},       // 0: the older per-song 7-string mode
    {L"neck", L"clite"},                // 0: the older per-song 7-string mode (no guitar rebuild)
};
const int kRs7RemovedKeyCount = int(sizeof(kRs7RemovedKeys) / sizeof(kRs7RemovedKeys[0]));

int Rs7Config_ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* iniPath, int hi) {
    return ReadInt(section, key, def, iniPath, hi);
}

std::wstring Rs7Config_ResolveIniPath(const wchar_t* iniPath) {
    if (!iniPath || !*iniPath) {
        return {};
    }

    const DWORD need = GetFullPathNameW(iniPath, 0, nullptr, nullptr);
    if (!need || need > 32768) {
        return {};
    }

    std::wstring path(need, L'\0');
    const DWORD n = GetFullPathNameW(iniPath, need, &path[0], nullptr);
    if (!n || n >= need) {
        return {};
    }

    path.resize(n);

    return path;
}

Rs7Config Rs7Config_Load(const wchar_t* iniPath) {
    Rs7Config c;
    const std::wstring path = Rs7Config_ResolveIniPath(iniPath);

    if (path.empty()) {
        Rs7Log("config: invalid/overlong ini path; defaults used");
        return c;
    }

    iniPath = path.c_str();
    const DWORD attr = GetFileAttributesW(iniPath);

    c.found = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    if (!c.found) {
        return c;
    }

    c.diagnosticsRaw = static_cast<int>(ReadInt(L"debug", L"diagnostics", 0, iniPath));
    c.diagnostics = c.diagnosticsRaw != 0;

    for (int i = 0; i < kRs7RemovedKeyCount; ++i) {
        wchar_t buf[4] = {};

        GetPrivateProfileStringW(kRs7RemovedKeys[i].section, kRs7RemovedKeys[i].key, L"", buf, 4, iniPath);
        if (buf[0] == 1 && !buf[1]) {
            continue;  // not in the file
        }

        ++c.removedKeys;
        Rs7Log("config: [%ls] %ls ignored, removed in 1.0", kRs7RemovedKeys[i].section, kRs7RemovedKeys[i].key);
    }

    // GetPrivateProfileString strips surrounding blanks; empty means "not set".
    bool tooLong = false;

    c.string6Raw = ReadString(L"color", L"string6_color", iniPath, &tooLong);
    if (!c.string6Raw.empty()) {
        c.colorValid = !tooLong && Rs7_ParseRgb(c.string6Raw.c_str(), &c.string6Rgb);
    }
    c.string6RgbCb = c.string6Rgb;
    c.string6CbRaw = ReadString(L"color", L"string6_color_cb", iniPath, &tooLong);
    c.colorCbSet = !c.string6CbRaw.empty();

    if (c.colorCbSet) {
        uint32_t cb = 0;

        c.colorCbValid = !tooLong && Rs7_ParseRgb(c.string6CbRaw.c_str(), &cb);
        if (c.colorCbValid) {
            c.string6RgbCb = cb;
        }
    }

    c.rsmodsExtendedRange = ReadInt(L"color", L"rsmods_extended_range", 1, iniPath) != 0;
    c.rampSwapRaw = static_cast<int>(ReadInt(L"color", L"ramp_swap", 1, iniPath));
    c.rampSwap = c.rampSwapRaw != 0;
    c.allowInexactRaw = static_cast<int>(ReadInt(L"color", L"allow_inexact", 0, iniPath));
    c.allowInexact = c.allowInexactRaw != 0;
    c.string7LineRaw = static_cast<int>(ReadInt(L"neck", L"string7_line", 1, iniPath));
    c.string7Line = c.string7LineRaw != 0;
    c.string7VRaw = ReadString(L"neck", L"string7_v", iniPath, &tooLong);

    if (!c.string7VRaw.empty()) {
        float v = 0.0f;

        c.string7VValid = !tooLong && Rs7_ParseAtlasRow(c.string7VRaw.c_str(), &v);
        if (c.string7VValid) {
            c.string7V = v;
        }
    }

    c.headstockPegRaw = static_cast<int>(ReadInt(L"neck", L"headstock_peg", 1, iniPath));
    c.headstockPeg = c.headstockPegRaw != 0;
    c.eightRaw = static_cast<int>(ReadInt(L"neck", L"eight", 0, iniPath));
    c.eight = c.eightRaw != 0;
    c.nativeChordloopsRaw = static_cast<int>(ReadInt(L"chords", L"native_chordloops", 1, iniPath));
    c.nativeChordloops = c.nativeChordloopsRaw != 0;
    c.fretExtendRaw = static_cast<int>(ReadInt(L"neck", L"fret_extend", 1, iniPath));
    c.fretExtend = c.fretExtendRaw != 0;
    c.fretHalfLenRaw = ReadString(L"neck", L"fret_halflen", iniPath, &tooLong);

    if (!c.fretHalfLenRaw.empty()) {
        float v = 0.0f;

        c.fretHalfLenValid = !tooLong && rs7frets::ParseHalfLen(c.fretHalfLenRaw.c_str(), &v);
        if (c.fretHalfLenValid) {
            c.fretHalfLen = v;
        }
    }

    c.tunerLowRangeRaw = static_cast<int>(ReadInt(L"tuner", L"lowrange", 0, iniPath));
    c.tunerLowRange = c.tunerLowRangeRaw != 0;
    const int at = static_cast<int>(ReadInt(L"debug", L"tuner_autotune", 0, iniPath, 30));

    c.tunerAutotune = at < 0 ? 0 : at > 30 ? 30 : at;
    c.songEndLog = ReadInt(L"debug", L"song_end_log", 1, iniPath) != 0;
    c.tunerNamesRaw = static_cast<int>(ReadInt(L"tuner", L"names", 1, iniPath));
    c.tunerNames = c.tunerNamesRaw != 0;

    // A present but empty value is a choice (no prefix / no suffix), so missing != empty here.
    for (int i = 0; i < 2; ++i) {
        const wchar_t* key = i ? L"name_suffix" : L"name_prefix";
        wchar_t buf[kValueMax] = {};
        const DWORD n = GetPrivateProfileStringW(L"tuner", key, L"\x01", buf, kValueMax, iniPath);
        if (n == 1 && buf[0] == 1) {
            continue;  // missing: the default
        }

        const std::wstring w(buf, n);

        if (!Rs7_ValidNameAffix(w)) {
            c.nameAffixValid = false;
            continue;
        }

        (i ? c.nameSuffix : c.namePrefix) = std::string(w.begin(), w.end());
    }

    return c;
}

bool Rs7_ParseAtlasRow(const wchar_t* text, float* out) {
    if (!text) {
        return false;
    }

    while (std::iswspace(*text)) {
        ++text;
    }

    if (!*text) {
        return false;
    }

    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text) {
        return false;
    }

    while (std::iswspace(*end)) {
        ++end;
    }

    if (*end || !std::isfinite(v) || v < 0.0 || v > 0.9 + 1e-6) {
        return false;
    }

    *out = static_cast<float>(v);

    return true;
}

bool Rs7_ValidNameAffix(const std::wstring& text) {
    if (text.size() > 31) {
        return false;
    }

    for (wchar_t ch : text) {
        if (ch < 0x20 || ch > 0x7E) {
            return false;
        }
    }

    return true;
}
