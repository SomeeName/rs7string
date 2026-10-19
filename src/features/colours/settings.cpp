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
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7ColourConfig g_config;

void ApplyColour(const Rs7SetValue&) {
    uint32_t rgb[2] = {Rs7Set_Rgb("color.string6_color"), Rs7Set_Rgb("color.string6_color_cb")};

    rs7rsmods::Resolve(rs7rsmods::Cached(), Rs7Set_Bool("color.rsmods_extended_range"), rgb);
    rs7colour::SetPalette(rgb);    // native colour matrix, 7th string palette / glow, tuner
    Rs7Swap_RequestRecolour(rgb);  // the gem ramps: repainted by the render thread (swap.h)
    rs7colour::PublishCurrent();
}

void StartupColour(Rs7SetValue& v) {
    v.rgb = g_config.string6Rgb;
}

void StartupColourCb(Rs7SetValue& v) {
    v.rgb = g_config.string6RgbCb;  // follows string6_color when missing or invalid
}

void Read(const wchar_t* iniPath) {
    g_config = Rs7Colour_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"color.rsmods_extended_range", L"color", L"rsmods_extended_range", T::Bool, W::Live, "1", 0, 1,
     "Follow RSMods Extended Range colour",
     "When RSMods is installed with ExtendedRange=on, use its low B colour in both colour modes. "
     "RSMods settings are read at startup. Turn off to use the two colours below; their saved values stay intact.",
     ApplyColour},
    {"color.string6_color", L"color", L"string6_color", T::Rgb, W::Live, "C8C8C8", 0, 0, "String 6 colour",
     "Gems (ramp copy), sparks, hit/miss text, 7th string line, glow, tuner.", ApplyColour, StartupColour},
    {"color.string6_color_cb", L"color", L"string6_color_cb", T::Rgb, W::Live, "C8C8C8", 0, 0,
     "String 6 colour (colourblind)", "The same in colourblind mode (default: string6_color).", ApplyColour,
     StartupColourCb},
    {"color.ramp_swap", L"color", L"ramp_swap", T::Bool, W::Restart, "1", 0, 1, "Ramp copy (Step 2)",
     "Copy the stock colour ramps with a string-6 band (D3D SetTexture/GetTexture hooks).", nullptr},
    {"color.allow_inexact", L"color", L"allow_inexact", T::Bool, W::Restart, "0", 0, 1, "Allow DXT ramps",
     "Also copy DXT1/DXT5 ramps (CPU-decoded, not bit-exact).", nullptr},
};
}  // namespace

const Rs7SetTable kRs7ColourSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

const char* Rs7Colour_ModeName() {
    uint32_t mgr = 0, mode = 0;
    if (!g_rs7_colormgr || !Rs7_SafeCopy(&mgr, reinterpret_cast<const void*>(g_rs7_colormgr), 4) || !mgr ||
        !Rs7_SafeCopy(&mode, reinterpret_cast<const void*>(mgr + 0x348), 4)) {
        return "unknown";
    }

    return mode == 0 ? "normal" : mode == 1 ? "colourblind" : "unknown";
}

namespace {
bool LiveV6(char* buf, size_t n) {
    Rs7SwapStats sw{};

    Rs7Swap_Stats(&sw);
    std::snprintf(buf, n, "hooks %s, %u copies, candidates: %u active / %u refused / %u retry / %u pending",
                  sw.installed ? "on" : "off", sw.copies, sw.active, sw.refused, sw.retry, sw.pending);

    return g_rs7_v6[0] != 0.0f;
}

bool LiveRampCopy(char* buf, size_t n) {
    Rs7SwapStats sw{};

    Rs7Swap_Stats(&sw);
    std::snprintf(buf, n, "%s; copies hold %06X / %06X",
                  g_rs7_v6[0] != 0.0f ? "V6 ON (gems use the copies)" : "V6 off (gems show the preview row)",
                  unsigned(sw.rgb[0]), unsigned(sw.rgb[1]));

    return true;
}

bool LiveColourMode(char* buf, size_t n) {
    std::snprintf(buf, n, "%s", Rs7Colour_ModeName());

    return true;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

// The two colours have no effect while the RSMods colour replaces them (same condition as ApplyColour).
bool ColoursOverridden() {
    return Rs7Set_Bool("color.rsmods_extended_range") && rs7rsmods::Cached().valid;
}

const Rs7UiItem kUi[] = {
    Rs7UiPlayerSection("Seventh string"),
    Rs7UiPlayerLocked("color.string6_color", "Colour", "Notes, string, glow and tuner of the seventh (low B) string.",
                      ColoursOverridden, "Turn off 'Use the RSMods Extended Range colour' to pick your own."),
    Rs7UiPlayerLocked("color.string6_color_cb", "Colourblind colour",
                      "Used instead when the game's colourblind mode is on.", ColoursOverridden,
                      "Turn off 'Use the RSMods Extended Range colour' to pick your own."),
    Rs7UiPlayer("color.rsmods_extended_range", "Use the RSMods Extended Range colour",
                "When RSMods Extended Range is on, its low B colour replaces the two colours above."),
    {U::Status, K::OnOff, "ramp copy (V6)", LiveV6},
    {U::Colour, K::Section, "string 6 colour"},
    {U::Colour, K::Row, "ramp copy", LiveRampCopy},
    {U::Colour, K::Row, "colour mode", LiveColourMode},
    {U::Colour, K::Note,
     "Colour changes reach sparks, hit/miss text, the 7th string line, its glow and the tuner at once; the gem ramps "
     "are repainted at the next texture bind."},
    {U::Colour, K::Section, "ramp copy (Step 2)"},
    {U::Colour, K::Setting, "color.ramp_swap"},
    {U::Colour, K::Setting, "color.allow_inexact"},
};
}  // namespace

const Rs7UiTable kRs7ColourUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const Rs7ColourConfig& Rs7Colour_Config() {
    return g_config;
}

Rs7ColourConfig Rs7Colour_ReadIni(const wchar_t* iniPath) {
    Rs7ColourConfig c;
    if (!iniPath) {
        return c;
    }

    // GetPrivateProfileString strips surrounding blanks; empty means "not set".
    bool tooLong = false;

    c.string6Raw = Rs7Config_ReadString(L"color", L"string6_color", iniPath, &tooLong);
    if (!c.string6Raw.empty()) {
        c.colorValid = !tooLong && Rs7_ParseRgb(c.string6Raw.c_str(), &c.string6Rgb);
    }
    c.string6RgbCb = c.string6Rgb;
    c.string6CbRaw = Rs7Config_ReadString(L"color", L"string6_color_cb", iniPath, &tooLong);
    c.colorCbSet = !c.string6CbRaw.empty();

    if (c.colorCbSet) {
        uint32_t cb = 0;

        c.colorCbValid = !tooLong && Rs7_ParseRgb(c.string6CbRaw.c_str(), &cb);
        if (c.colorCbValid) {
            c.string6RgbCb = cb;
        }
    }

    c.rsmodsExtendedRange = Rs7Config_ReadInt(L"color", L"rsmods_extended_range", 1, iniPath) != 0;
    c.rampSwapRaw = Rs7Config_ReadInt(L"color", L"ramp_swap", 1, iniPath);
    c.rampSwap = c.rampSwapRaw != 0;
    c.allowInexactRaw = Rs7Config_ReadInt(L"color", L"allow_inexact", 0, iniPath);
    c.allowInexact = c.allowInexactRaw != 0;

    return c;
}
