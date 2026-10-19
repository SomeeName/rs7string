// The fret bars' settings: [neck] fret_extend / fret_halflen (overlay and template) and their startup read.
#include "platform/settings.h"
#include "features/frets/frets.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7FretConfig g_config;

void ApplyFrets(const Rs7SetValue& v) {
    g_rs7_frets_on = v.i ? 1 : 0;
}  // F2 restores / applies (frets.h)

void ApplyHalfLen(const Rs7SetValue& v) {
    g_rs7_fret_halflen = v.f[0];
}  // F2 re-applies on a change

void StartupHalfLen(Rs7SetValue& v) {
    v.f[0] = g_config.fretHalfLen;
}

void Read(const wchar_t* iniPath) {
    g_config = Rs7Frets_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"neck.fret_extend", L"neck", L"fret_extend", T::Bool, W::Live, "1", 0, 1, "Fret wires span the 7th string",
     "F1/F2; off restores the stock bars at the next frame.", ApplyFrets},
    {"neck.fret_halflen", L"neck", L"fret_halflen", T::Float, W::Live, "4.61", 1, 20, "Fret bar half length",
     "L in k = (L + d/2) / L; re-applied at the next frame.", ApplyHalfLen, StartupHalfLen},
};
}  // namespace

const Rs7SetTable kRs7FretSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LiveFrets(char* buf, size_t n) {
    Rs7FretsStats fr{};

    Rs7Frets_Stats(&fr);
    std::snprintf(buf, n, "%d guitar(s) applied, L = %.3f", fr.applied, double(g_rs7_fret_halflen));

    return g_rs7_frets_on != 0;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    Rs7UiPlayer("neck.fret_extend", "Frets across the seventh string",
                "Lengthens the frets on seven-string songs. Off keeps the normal length."),
    {U::Status, K::OnOff, "fret extend", LiveFrets},
    {U::Neck, K::Section, "fret wires"},
    {U::Neck, K::Setting, "neck.fret_halflen"},
};
}  // namespace

const Rs7UiTable kRs7FretUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const Rs7FretConfig& Rs7Frets_Config() {
    return g_config;
}

Rs7FretConfig Rs7Frets_ReadIni(const wchar_t* iniPath) {
    Rs7FretConfig c;
    if (!iniPath) {
        return c;
    }

    bool tooLong = false;

    c.fretExtendRaw = Rs7Config_ReadInt(L"neck", L"fret_extend", 1, iniPath);
    c.fretExtend = c.fretExtendRaw != 0;
    c.fretHalfLenRaw = Rs7Config_ReadString(L"neck", L"fret_halflen", iniPath, &tooLong);

    if (!c.fretHalfLenRaw.empty()) {
        float v = 0.0f;

        c.fretHalfLenValid = !tooLong && rs7frets::ParseHalfLen(c.fretHalfLenRaw.c_str(), &v);
        if (c.fretHalfLenValid) {
            c.fretHalfLen = v;
        }
    }

    return c;
}
