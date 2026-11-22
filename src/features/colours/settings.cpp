// The colour feature's settings: the [color] keys (overlay and template), their startup read, the live colour change,
// and its overlay tab (Colour) and status row.
#include "platform/settings.h"
#include "features/colours/colours.h"
#include "model/groups.h"
#include "platform/config.h"
#include "platform/safecopy.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

RsxrColourConfig g_config;

// The seventh string's (string 6's) colours as set, with the RSMods Extended Range colour applied.
void EffectiveString6Rgb(uint32_t (&rgb)[2]) {
    rgb[0] = RsxrSet_Rgb("color.string6_color");
    rgb[1] = RsxrSet_Rgb("color.string6_color_cb");
    rsxrrsmods::Resolve(rsxrrsmods::Cached(), RsxrSet_Bool("color.rsmods_extended_range"), rgb);
}

void ApplyColour(const RsxrSetValue&) {
    uint32_t rgb[2];

    EffectiveString6Rgb(rgb);
    rsxrcolour::SetPalette(rgb);    // native colour matrix, 7th string palette / glow, tuner
    RsxrSwap_RequestRecolour(rgb);  // the gem ramps: repainted by the render thread (swap.h)
    rsxrcolour::PublishCurrent();
}

// String 7 (the eighth string): its line, glow, sparks and gems; RSMods has no colour for it.
void ApplyColour8(const RsxrSetValue&) {
    const uint32_t rgb8[2] = {RsxrSet_Rgb("color.string7_color"), RsxrSet_Rgb("color.string7_color_cb")};
    uint32_t rgb[2];

    rsxrcolour::SetPalette8(rgb8);
    EffectiveString6Rgb(rgb);
    RsxrSwap_RequestRecolour(rgb);  // the gem ramps repaint the eighth string's band too (BuildChecked)
    rsxrcolour::PublishCurrent();
}

void StartupColour8(RsxrSetValue& v) {
    v.rgb = g_config.string7Rgb;
}

void StartupColour8Cb(RsxrSetValue& v) {
    v.rgb = g_config.string7RgbCb;
}

void StartupColour(RsxrSetValue& v) {
    v.rgb = g_config.string6Rgb;
}

void StartupColourCb(RsxrSetValue& v) {
    v.rgb = g_config.string6RgbCb;  // follows string6_color when missing or invalid
}

void Read(const wchar_t* iniPath) {
    g_config = RsxrColour_ReadIni(iniPath);
}

const RsxrSetDef kDefs[] = {
    {"color.rsmods_extended_range", L"color", L"rsmods_extended_range", T::Bool, W::Live, "1", 0, 1,
     "Use the RSMods Extended Range colour",
     "When RSMods is installed with ExtendedRange=on, use its low B colour in both colour modes. "
     "RSMods settings are read at startup. Turn off to use the two colours below; their saved values stay intact.",
     ApplyColour},
    {"color.string6_color", L"color", L"string6_color", T::Rgb, W::Live, "C8C8C8", 0, 0, "7th string colour",
     "The 7th string's gems, sparks, hit/miss text, string line, glow and tuner, and a 5/6-string bass's low B.",
     ApplyColour, StartupColour},
    {"color.string6_color_cb", L"color", L"string6_color_cb", T::Rgb, W::Live, "C8C8C8", 0, 0,
     "7th string colour (colourblind)", "The same in colourblind mode (default: string6_color).", ApplyColour,
     StartupColourCb},
    {"color.string7_color", L"color", L"string7_color", T::Rgb, W::Live, "FF66CC", 0, 0, "8th string colour",
     "The 8th string's gems, string line, glow and sparks.", ApplyColour8, StartupColour8},
    {"color.string7_color_cb", L"color", L"string7_color_cb", T::Rgb, W::Live, "FF66CC", 0, 0,
     "8th string colour (colourblind)", "The same in colourblind mode.", ApplyColour8, StartupColour8Cb},
};
}  // namespace

const RsxrSetTable kRsxrColourSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool ReadMode(uint32_t* mode) {
    uint32_t mgr = 0;

    return g_rsxr_colormgr && Rsxr_SafeCopy(&mgr, reinterpret_cast<const void*>(g_rsxr_colormgr), 4) && mgr &&
           Rsxr_SafeCopy(mode, reinterpret_cast<const void*>(mgr + 0x348), 4) && *mode <= 1;
}
}  // namespace

const char* RsxrColour_ModeName() {
    uint32_t mode = 0;
    if (!ReadMode(&mode)) {
        return "unknown";
    }

    return mode == 0 ? "normal" : "colourblind";
}

uint32_t RsxrColour_Mode() {
    uint32_t mode = 0;

    return ReadMode(&mode) ? mode : 0;
}

namespace {
bool LiveV6(char* buf, size_t n) {
    RsxrSwapStats sw{};

    RsxrSwap_Stats(&sw);
    std::snprintf(buf, n, "hooks %s, %u copies, candidates: %u active / %u refused / %u retry / %u pending",
                  sw.installed ? "on" : "off", sw.copies, sw.active, sw.refused, sw.retry, sw.pending);

    return g_rsxr_v6[0] != 0.0f;
}

bool LiveRampCopy(char* buf, size_t n) {
    RsxrSwapStats sw{};

    RsxrSwap_Stats(&sw);
    std::snprintf(buf, n, "%s; copies hold %06X / %06X",
                  g_rsxr_v6[0] != 0.0f ? "V6 ON (gems use the copies)" : "V6 off (gems show the preview row)",
                  unsigned(sw.rgb[0]), unsigned(sw.rgb[1]));

    return true;
}

bool LiveColourMode(char* buf, size_t n) {
    std::snprintf(buf, n, "%s", RsxrColour_ModeName());

    return true;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

// The two colours have no effect while the RSMods colour replaces them (same condition as ApplyColour).
bool ColoursOverridden() {
    return RsxrSet_Bool("color.rsmods_extended_range") && rsxrrsmods::Cached().valid;
}

const RsxrUiItem kUi[] = {
    RsxrUiPlayerSection("Extra strings"),
    RsxrUiPlayer("color.rsmods_extended_range", "Use the RSMods Extended Range colour",
                 "When RSMods Extended Range is on, its low B colour replaces the two 7th string colours below."),
    RsxrUiPlayerLocked("color.string6_color", "7th string colour",
                       "Notes, string, glow and tuner of the 7th (low B) string.", ColoursOverridden,
                       "Turn off 'Use the RSMods Extended Range colour' to pick your own."),
    RsxrUiPlayerLocked("color.string6_color_cb", "7th string colour (colourblind)",
                       "Used instead when the game's colourblind mode is on.", ColoursOverridden,
                       "Turn off 'Use the RSMods Extended Range colour' to pick your own."),
    RsxrUiPlayer("color.string7_color", "8th string colour", "String, glow and sparks of the 8th (low F#) string."),
    RsxrUiPlayer("color.string7_color_cb", "8th string colour (colourblind)",
                 "Used instead when the game's colourblind mode is on."),
    {U::Status, K::OnOff, "ramp copy (V6)", LiveV6},
    {U::Colour, K::Section, "string 6 colour"},
    {U::Colour, K::Row, "ramp copy", LiveRampCopy},
    {U::Colour, K::Row, "colour mode", LiveColourMode},
    {U::Colour, K::Note,
     "Colour changes reach sparks, hit/miss text, the 7th string line, its glow and the tuner at once; the gem ramps "
     "are repainted at the next texture bind."},
};
}  // namespace

const RsxrUiTable kRsxrColourUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const RsxrColourConfig& RsxrColour_Config() {
    return g_config;
}

namespace {
// One [color] key: `raw` gets the text (GetPrivateProfileString strips blanks; empty = not set). A valid colour goes
// into `rgb`; an invalid one leaves `rgb` at its default and returns false.
bool ReadColour(const wchar_t* key, const wchar_t* iniPath, std::wstring& raw, uint32_t& rgb) {
    bool tooLong = false;
    uint32_t v = 0;

    raw = RsxrConfig_ReadString(L"color", key, iniPath, &tooLong);
    if (raw.empty()) {
        return true;
    }
    if (tooLong || !Rsxr_ParseRgb(raw.c_str(), &v)) {
        return false;
    }

    rgb = v;

    return true;
}
}  // namespace

RsxrColourConfig RsxrColour_ReadIni(const wchar_t* iniPath) {
    RsxrColourConfig c;
    if (!iniPath) {
        return c;
    }

    // The eighth string's colours fall back to their own default; string6_color_cb follows string6_color.
    std::wstring raw7;

    c.colorValid = ReadColour(L"string6_color", iniPath, c.string6Raw, c.string6Rgb);
    c.string6RgbCb = c.string6Rgb;
    c.colorCbValid = ReadColour(L"string6_color_cb", iniPath, c.string6CbRaw, c.string6RgbCb);
    c.colorCbSet = !c.string6CbRaw.empty();
    c.color7Valid = ReadColour(L"string7_color", iniPath, raw7, c.string7Rgb);
    c.color7CbValid = ReadColour(L"string7_color_cb", iniPath, raw7, c.string7RgbCb);
    c.rsmodsExtendedRange = RsxrSet_ReadTableInt(kRsxrColourSettings, "color.rsmods_extended_range", iniPath) != 0;

    return c;
}
