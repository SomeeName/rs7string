// Fuzz targets: the settings store's text parse (platform/settings.h), the ini reads of every feature
// (platform/config.h, the settings tables) and the small text parsers (pipe commands, hotkeys, key lines, colours).
#include "dinput_wrap.h"
#include "features/extras/extras.h"
#include "features/spacing/spacing.h"
#include "fuzz.h"
#include "overlay/overlaycmd.h"
#include "platform/config.h"
#include "platform/settings.h"
#include <windows.h>
#include <cstdio>
#include <string>

namespace rsxrfuzz {
namespace {
std::string Str(const uint8_t* d, size_t n, size_t max) {
    std::string s(reinterpret_cast<const char*>(d), n < max ? n : max);

    return s.substr(0, s.find('\0'));
}

const std::wstring& IniFile() {
    static const std::wstring path = [] {
        wchar_t dir[MAX_PATH] = {};

        GetTempPathW(MAX_PATH, dir);
        wchar_t name[64];

        std::swprintf(name, 64, L"rsxrfuzz-%lu.ini", GetCurrentProcessId());

        return std::wstring(dir) + name;
    }();

    return path;
}
}  // namespace

// [key index lo][key index hi][value text...]: RsxrSet_SetText for that key, then its formatted value.
int Settings(const uint8_t* d, size_t n) {
    if (n < 2 || RsxrSet_Count() == 0) {
        return 0;
    }

    const int i = (d[0] | d[1] << 8) % RsxrSet_Count();
    const std::string text = Str(d + 2, n - 2, 400);

    RsxrSet_SetText(RsxrSet_Def(i).id, text.c_str());
    RsxrSet_Format(i, RsxrSet_State(i).value);
    RsxrSet_Find(text.c_str());
    RsxrSet_ResetForTests();

    return 0;
}

Seeds SettingsSeeds() {
    Seeds out;

    for (int i = 0; i < RsxrSet_Count(); ++i) {
        std::vector<uint8_t> s = {uint8_t(i), uint8_t(i >> 8)};
        const std::vector<uint8_t> v = Bytes(RsxrSet_Def(i).def ? RsxrSet_Def(i).def : "");

        s.insert(s.end(), v.begin(), v.end());
        out.push_back(s);
    }

    return out;
}

// The bytes as rsxr.ini: every feature's startup read and the overlay's view of every key. Starts from the
// defaults, so a finding reproduces from its one input; the file is deleted again.
int Ini(const uint8_t* d, size_t n) {
    RsxrSet_ResetForTests();

    if (FILE* f = _wfopen(IniFile().c_str(), L"wb")) {
        std::fwrite(d, 1, n, f);
        std::fclose(f);
    }

    RsxrSet_ReadIni(IniFile().c_str());
    RsxrSet_InitFromIni(IniFile().c_str());
    DeleteFileW(IniFile().c_str());

    return 0;
}

Seeds IniSeeds() {
    std::string all;

    for (int i = 0; i < RsxrSet_Count(); ++i) {
        const RsxrSetDef& def = RsxrSet_Def(i);
        char line[512];

        std::snprintf(line, sizeof line, "[%ls]\r\n%ls=%s\r\n", def.section, def.key, def.def ? def.def : "");
        all += line;
    }

    return {Bytes(all), Bytes("[neck]\r\nspacing_guitar7=1.34\r\n[color]\r\nstring6_color=#FF00FF\r\n"),
            Bytes("\xFF\xFE[x]"),
            // Over kRsxrIniValueMax characters: the value is invalid, not cut short (platform/config.h).
            Bytes("[color]\r\nstring6_color=C8C8C8" + std::string(300, ' ') + "junk\r\n")};
}

// [parser][text...]
int Text(const uint8_t* d, size_t n) {
    if (n < 1) {
        return 0;
    }

    const std::string s = Str(d + 1, n - 1, 600);
    const std::wstring w = Widen(d + 1, n - 1);
    RsxrCmd cmd;
    uint8_t dik = 0;
    bool down = false;
    uint32_t rgb = 0;
    float f = 0;

    switch (d[0] % 9) {
        case 0:
            RsxrCmd_Parse(s.c_str(), &cmd);
            break;
        case 1:
            Rsxr_ParseHotkey(s.c_str());
            break;
        case 2:
            RsxrDinput_ParseKeyLine(s.c_str(), &dik, &down);
            break;
        case 3:
            Rsxr_ParseRgb(w.c_str(), &rgb);
            break;
        case 4:
            rsxrspacing::Parse(w.c_str(), &f);
            break;
        default:
            RsxrPrivate_FuzzText(d[0] % 9, w.c_str());  // the private extras' parsers (src/features/extras/extras.h)
            break;
    }

    return 0;
}

Seeds TextSeeds() {
    return {Bytes(std::string("\x00o set neck.spacing_guitar7 1.5", 31)),
            Bytes("\x01"
                  "F9"),
            Bytes("\x02"
                  "down 0x1C"),
            {3, '#', 0, 'F', 0, 'F', 0, '0', 0, '0', 0, 'F', 0, 'F', 0},
            {4, '0', 0, '.', 0, '5', 0},
            {7, 'a', 0, 'l', 0, 'l', 0}};
}
}  // namespace rsxrfuzz
