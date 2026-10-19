// [test] force_colourblind / force_lefty / force_headstock (testdrive.h): the keys, read once at startup, and the
// values the hooks (hooks_variants.cpp) hand the game instead of the profile's.
#include "features/testdrive/testdrive.h"
#include "platform/config.h"
#include "platform/log.h"
#include <cwchar>
#include <string>

namespace {
Rs7Variant g_variant;
}  // namespace

int Rs7Variant_ParseHeadstock(const wchar_t* text) {
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

Rs7GuitarFlags Rs7Variant_Apply(const Rs7Variant& v, Rs7GuitarFlags profile) {
    Rs7GuitarFlags out = profile;

    if (v.colourblind) {
        out.colourblind = 1;
    }
    if (v.lefty) {
        out.lefty = 1;
    }
    if (v.headstock == 33) {
        out.inlineHead = 0;
    } else if (v.headstock == 6) {
        out.inlineHead = 1;
    }

    return out;
}

const char* Rs7Variant_ColourModeText(const Rs7Variant& v, const char* profile) {
    return v.colourblind ? "ColorBlindMild" : profile;
}

void Rs7Variant_ReadIni(const wchar_t* iniPath) {
    Rs7Variant v;

    v.colourblind = Rs7Config_ReadInt(L"test", L"force_colourblind", 0, iniPath) != 0;
    v.lefty = Rs7Config_ReadInt(L"test", L"force_lefty", 0, iniPath) != 0;

    bool tooLong = false;
    const std::wstring hs = Rs7Config_ReadString(L"test", L"force_headstock", iniPath, &tooLong);
    const int parsed = tooLong ? -1 : Rs7Variant_ParseHeadstock(hs.c_str());

    v.headstock = parsed < 0 ? 0 : parsed;
    g_variant = v;
    if (parsed < 0) {
        Rs7Log("config: [test] force_headstock text \"%ls\" is not 33 or 6 - ignored", hs.c_str());
    }

    if (!v.Any()) {
        Rs7Log("config: [test] force_colourblind/force_lefty/force_headstock off (the profile's settings)");
        return;
    }

    Rs7Log("config: [test] force_colourblind=%d force_lefty=%d force_headstock=%s (test override, runtime only, "
           "never saved to the profile)",
           v.colourblind ? 1 : 0, v.lefty ? 1 : 0,
           v.headstock == 33  ? "33"
           : v.headstock == 6 ? "6"
                              : "profile");
}

const Rs7Variant& Rs7Variant_Get() {
    return g_variant;
}
