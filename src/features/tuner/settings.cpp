// The tuner's settings: [debug] tuner_autotune and [tuner] (overlay and template), their startup read.
#include "platform/settings.h"
#include "features/tuner/tuner.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>
#include <intrin.h>

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

RsxrTunerConfig g_config;

void ApplyAutotune(const RsxrSetValue& v) {
    g_rsxr_tuner_auto = static_cast<uintptr_t>(v.i);
    // The tuner stubs only enter C++ while g_rsxr_tuner_any is set; the tuner recomputes it at its
    // next event (UpdateAny), so set it now: any = 1 without a session is the autotune state.
    if (v.i) {
        _InterlockedExchange(&g_rsxr_tuner_any, 1);
    }
}

void ApplyLowRange(const RsxrSetValue& v) {
    g_rsxr_lowrange = v.i ? 1 : 0;
}  // W: read per check

void StartupAutotune(RsxrSetValue& v) {
    v.i = g_config.tunerAutotune;
}

void Read(const wchar_t* iniPath) {
    g_config = RsxrTuner_ReadIni(iniPath);
}

const RsxrSetDef kDefs[] = {
    // [debug]
    {"debug.tuner_autotune", L"debug", L"tuner_autotune", T::Int, W::Live, "0", 0, 30, "Tuner autotune (s)",
     "Test driving: each tuner string reports tuned N s after it was selected. 0 = off.", ApplyAutotune,
     StartupAutotune},
    // [tuner]
    {"tuner.lowrange", L"tuner", L"lowrange", T::Bool, W::Live, "1", 0, 1, "Low-range tuner",
     "7-string tuner: the wrong-instrument warning and the detector's lowest open follow the lowest string.",
     ApplyLowRange},
    {"tuner.names", L"tuner", L"names", T::Bool, W::Restart, "1", 0, 1, "Extended tuning names",
     "The song list, song pages and tuner name extended tunings, e.g. B Standard, Drop A. Applies after a restart.",
     nullptr},
    {"tuner.name_prefix", L"tuner", L"name_prefix", T::Text, W::Restart, "", 0, 31, "Name prefix",
     "Text before an extended tuning name (e.g. 7Str -> 7Str B Standard). Empty: none. Applies after a restart.",
     nullptr},
    {"tuner.name_suffix", L"tuner", L"name_suffix", T::Text, W::Restart, "7Str", 0, 31, "Name suffix",
     "Text after an extended tuning name (default 7Str -> B Standard 7Str; 8Str, 5Str, 6Str on the other "
     "instruments). Empty: none. Applies after a restart.",
     nullptr},
};
}  // namespace

const RsxrSetTable kRsxrTunerSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LiveTuner(char* buf, size_t n) {
    RsxrTunerStats ts{};

    RsxrTuner_Stats(&ts);
    std::snprintf(buf, n, "%d native session(s)", ts.sessions);

    return g_rsxr_native_tuner != 0;
}

bool LiveSessions(char* buf, size_t n) {
    RsxrTunerStats ts{};

    RsxrTuner_Stats(&ts);
    std::snprintf(buf, n, "%d native session(s) now, tuner hooks %s", ts.sessions, ts.any ? "active" : "idle");

    return true;
}

using U = RsxrUiTab;
using K = RsxrUiKind;

const RsxrUiItem kUi[] = {
    RsxrUiPlayerSection("Tuner and tuning names", "Tuner"),
    RsxrUiPlayer("tuner.lowrange", "Tuner for very low tunings",
                 "Stops the wrong-instrument warning in Drop A, A Standard and G Standard."),
    RsxrUiPlayer("tuner.names", "Extended tuning names",
                 "Song list, song pages and tuner show names such as B Standard. Applies after a restart."),
    RsxrUiPlayer("tuner.name_prefix", "Text before the tuning name",
                 "For example 7Str gives 7Str B Standard. Can be empty. Applies after a restart."),
    RsxrUiPlayer("tuner.name_suffix", "Text after the tuning name",
                 "For example 7Str gives B Standard 7Str. Can be empty. Applies after a restart."),
    {U::Status, K::OnOff, "7-string tuner", LiveTuner},
    {U::Tuner, K::Section, "7-string tuner"},
    {U::Tuner, K::Row, "sessions", LiveSessions},
};
}  // namespace

const RsxrUiTable kRsxrTunerUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const RsxrTunerConfig& RsxrTuner_Config() {
    return g_config;
}

RsxrTunerConfig RsxrTuner_ReadIni(const wchar_t* iniPath) {
    RsxrTunerConfig c;
    if (!iniPath) {
        return c;
    }

    c.tunerLowRangeRaw = RsxrSet_ReadTableInt(kRsxrTunerSettings, "tuner.lowrange", iniPath);
    c.tunerLowRange = c.tunerLowRangeRaw != 0;
    c.tunerAutotune = RsxrSet_ReadTableInt(kRsxrTunerSettings, "debug.tuner_autotune", iniPath);
    c.tunerNamesRaw = RsxrSet_ReadTableInt(kRsxrTunerSettings, "tuner.names", iniPath);
    c.tunerNames = c.tunerNamesRaw != 0;

    // A present but empty value is a choice (no prefix / no suffix), so missing != empty here.
    for (int i = 0; i < 2; ++i) {
        std::wstring w;
        if (!RsxrConfig_ReadPresent(L"tuner", i ? L"name_suffix" : L"name_prefix", iniPath, &w)) {
            continue;  // missing: the default
        }

        if (!Rsxr_ValidNameAffix(w)) {
            c.nameAffixValid = false;
            continue;
        }

        std::string ascii;  // Rsxr_ValidNameAffix: printable ASCII only

        for (wchar_t ch : w) {
            ascii.push_back(char(ch));
        }

        (i ? c.nameSuffix : c.namePrefix) = ascii;
    }

    return c;
}
