// The string spacing settings: [neck] spacing_bass5 / spacing_bass6 / spacing_guitar7 / spacing_guitar8 (overlay and
// template) and their startup read.
#include "platform/settings.h"
#include "features/spacing/spacing.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/uilayout.h"
#include <string>

float g_rsxr_spacing[4] = {};

bool rsxrspacing::Parse(const wchar_t* text, float* out) {
    float v = 0.0f;
    // All four rows share the range 0 .. kMax; below kMin is stock (0), as the overlay's Apply does.
    const bool ok = !text || !*text || RsxrSet_ParseTableFloat(kRsxrSpacingSettings, "neck.spacing_guitar7", text, &v);

    *out = OrStock(v);

    return ok;
}

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

const wchar_t* const kKeys[4] = {L"spacing_bass5", L"spacing_bass6", L"spacing_guitar7", L"spacing_guitar8"};

template <int I>
void Apply(const RsxrSetValue& v) {
    g_rsxr_spacing[I] = rsxrspacing::OrStock(v.f[0]);  // as the ini read (Parse)
}

template <int I>
void Startup(RsxrSetValue& v) {
    v.f[0] = g_rsxr_spacing[I];
}

void Read(const wchar_t* iniPath) {
    for (int i = 0; i < 4; ++i) {
        g_rsxr_spacing[i] = 0.0f;
        if (!iniPath) {
            continue;
        }

        bool tooLong = false;
        const std::wstring raw = RsxrConfig_ReadString(L"neck", kKeys[i], iniPath, &tooLong);
        float v = 0.0f;

        if (!tooLong && rsxrspacing::Parse(raw.c_str(), &v)) {
            g_rsxr_spacing[i] = v;
        } else {
            RsxrLog("settings: [neck] %ls=%ls is not a spacing of 0-4: stock spacing", kKeys[i], raw.c_str());
        }
    }
}

const RsxrSetDef kDefs[] = {
    {"neck.spacing_bass5", L"neck", L"spacing_bass5", T::Float, W::Live, "0", 0, 4, "5-string bass string spacing",
     "Distance between the strings of a 5-string bass on the neck and highway. 0 = stock bass spacing 2.68 (10.7 "
     "wide); 2.01 keeps the 4-string bass width, 1.608 is the guitar spacing. Takes effect when the bass is next "
     "built (the next song with another instrument or string count, or a restart).",
     Apply<0>, Startup<0>},
    {"neck.spacing_bass6", L"neck", L"spacing_bass6", T::Float, W::Live, "0", 0, 4, "6-string bass string spacing",
     "Distance between the strings of a 6-string bass. 0 = stock bass spacing 2.68 (13.4 wide); 1.608 (the guitar "
     "spacing) keeps the 4-string bass width. Takes effect when the bass is next built.",
     Apply<1>, Startup<1>},
    {"neck.spacing_guitar7", L"neck", L"spacing_guitar7", T::Float, W::Live, "0", 0, 4,
     "7-string guitar string spacing",
     "Distance between the strings of a 7-string guitar. 0 = stock guitar spacing 1.608 (9.6 wide); 1.34 keeps the "
     "6-string width. Takes effect when the guitar is next built.",
     Apply<2>, Startup<2>},
    {"neck.spacing_guitar8", L"neck", L"spacing_guitar8", T::Float, W::Live, "0", 0, 4,
     "8-string guitar string spacing",
     "Distance between the strings of an 8-string guitar. 0 = stock guitar spacing 1.608 (11.3 wide); 1.15 keeps "
     "the 6-string width. Takes effect when the guitar is next built.",
     Apply<3>, Startup<3>},
};
}  // namespace

const RsxrSetTable kRsxrSpacingSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    {U::Neck, K::Section, "string spacing"},       {U::Neck, K::Setting, "neck.spacing_bass5"},
    {U::Neck, K::Setting, "neck.spacing_bass6"},   {U::Neck, K::Setting, "neck.spacing_guitar7"},
    {U::Neck, K::Setting, "neck.spacing_guitar8"},
};
}  // namespace

const RsxrUiTable kRsxrSpacingUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
