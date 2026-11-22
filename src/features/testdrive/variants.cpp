// [test] force_colourblind / force_lefty / force_headstock (testdrive.h): the keys, read once at startup, and the
// values the hooks (hooks_variants.cpp) hand the game instead of the profile's.
#include "features/testdrive/testdrive.h"
#include "platform/config.h"
#include "platform/log.h"
#include <cwchar>
#include <string>

namespace {
RsxrVariant g_variant;

// A three-way [test] key (RsxrVariant_ParseForce); unknown text is logged and ignored (0).
int ReadForce(const wchar_t* key, const wchar_t* on, const wchar_t* off, const wchar_t* iniPath) {
    bool tooLong = false;
    const std::wstring text = RsxrConfig_ReadString(L"test", key, iniPath, &tooLong);
    const int v = tooLong ? -2 : RsxrVariant_ParseForce(text.c_str(), on, off);

    if (v == -2) {
        RsxrLog("config: [test] %ls text \"%ls\" is not 0, 1 / %ls or -1 / %ls - ignored", key, text.c_str(), on, off);
        return 0;
    }

    return v;
}
}  // namespace

int RsxrVariant_ParseHeadstock(const wchar_t* text) {
    if (!text || !*text || !std::wcscmp(text, L"0")) {
        return 0;
    }
    if (!_wcsicmp(text, L"33") || !_wcsicmp(text, L"3+3")) {
        return 33;
    }
    if (!_wcsicmp(text, L"6") || !_wcsicmp(text, L"inline") || !_wcsicmp(text, L"6-in-line")) {
        return 6;
    }

    return -1;
}

int RsxrVariant_ParseForce(const wchar_t* text, const wchar_t* on, const wchar_t* off) {
    if (!text || !*text || !std::wcscmp(text, L"0")) {
        return 0;
    }
    if (!std::wcscmp(text, L"1") || !_wcsicmp(text, on)) {
        return 1;
    }
    if (!std::wcscmp(text, L"-1") || !_wcsicmp(text, off)) {
        return -1;
    }

    return -2;
}

RsxrGuitarFlags RsxrVariant_Apply(const RsxrVariant& v, RsxrGuitarFlags profile) {
    RsxrGuitarFlags out = profile;

    if (v.colourblind) {
        out.colourblind = 1;
    } else if (v.normalColours) {
        out.colourblind = 0;
    }
    if (v.lefty) {
        out.lefty = 1;
    } else if (v.righty) {
        out.lefty = 0;
    }
    if (v.headstock == 33) {
        out.inlineHead = 0;
    } else if (v.headstock == 6) {
        out.inlineHead = 1;
    }

    return out;
}

const char* RsxrVariant_ColourModeText(const RsxrVariant& v, const char* profile) {
    return v.colourblind ? "ColorBlindMild" : v.normalColours ? "ColorBlindNone" : profile;
}

uint32_t RsxrVariant_ColourMode(const RsxrVariant& v, uint32_t current) {
    return v.colourblind ? 1u : current;
}

void RsxrVariant_ReadIni(const wchar_t* iniPath) {
    RsxrVariant v;

    const int colourblind = ReadForce(L"force_colourblind", L"on", L"off", iniPath);
    const int lefty = ReadForce(L"force_lefty", L"lh", L"rh", iniPath);

    v.colourblind = colourblind == 1;
    v.normalColours = colourblind == -1;
    v.lefty = lefty == 1;
    v.righty = lefty == -1;

    bool tooLong = false;
    const std::wstring hs = RsxrConfig_ReadString(L"test", L"force_headstock", iniPath, &tooLong);
    const int parsed = tooLong ? -1 : RsxrVariant_ParseHeadstock(hs.c_str());

    v.headstock = parsed < 0 ? 0 : parsed;
    g_variant = v;
    if (parsed < 0) {
        RsxrLog("config: [test] force_headstock text \"%ls\" is not 33 or 6 - ignored", hs.c_str());
    }

    if (!v.Any()) {
        RsxrLog("config: [test] force_colourblind/force_lefty/force_headstock off (the profile's settings)");
        return;
    }

    RsxrLog("config: [test] force_colourblind=%d force_lefty=%d force_headstock=%s (test override, runtime only, "
            "never saved to the profile)",
            v.colourblind     ? 1
            : v.normalColours ? -1
                              : 0,
            v.lefty    ? 1
            : v.righty ? -1
                       : 0,
            v.headstock == 33  ? "33"
            : v.headstock == 6 ? "6"
                               : "profile");
}

const RsxrVariant& RsxrVariant_Get() {
    return g_variant;
}
