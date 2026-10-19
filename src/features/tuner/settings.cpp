// The tuner's settings: [debug] tuner_autotune and [tuner] (overlay and template), their startup read.
#include "platform/settings.h"
#include "features/tuner/tuner.h"
#include "platform/config.h"
#include "platform/uilayout.h"
#include <cstdio>
#include <intrin.h>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7TunerConfig g_config;

void ApplyAutotune(const Rs7SetValue& v) {
    g_rs7_tuner_auto = static_cast<uintptr_t>(v.i);
    // The tuner stubs only enter C++ while g_rs7_tuner_any is set; the tuner recomputes it at its
    // next event (UpdateAny), so set it now: any = 1 without a session is the autotune state.
    if (v.i) {
        _InterlockedExchange(&g_rs7_tuner_any, 1);
    }
}

void ApplyLowRange(const Rs7SetValue& v) {
    g_rs7_lowrange = v.i ? 1 : 0;
}  // W: read per check

void StartupAutotune(Rs7SetValue& v) {
    v.i = g_config.tunerAutotune;
}

void Read(const wchar_t* iniPath) {
    g_config = Rs7Tuner_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    // [debug]
    {"debug.tuner_autotune", L"debug", L"tuner_autotune", T::Int, W::Live, "0", 0, 30, "Tuner autotune (s)",
     "Test driving: each tuner string reports tuned N s after it was selected. 0 = off.", ApplyAutotune,
     StartupAutotune},
    // [tuner]
    {"tuner.lowrange", L"tuner", L"lowrange", T::Bool, W::Live, "1", 0, 1, "Low-range tuner",
     "7-string tuner: the wrong-instrument warning and the detector's lowest open follow the lowest string.",
     ApplyLowRange},
    {"tuner.names", L"tuner", L"names", T::Bool, W::Restart, "1", 0, 1, "7-string tuning names",
     "Song list, song pages and tuner name 7-string arrangements (Rs7String6) as B Standard, Drop A, ...", nullptr},
    {"tuner.name_prefix", L"tuner", L"name_prefix", T::Text, W::Restart, "", 0, 31, "Name prefix",
     "Text before a 7-string tuning name (e.g. 7Str -> 7Str B Standard). Empty: none.", nullptr},
    {"tuner.name_suffix", L"tuner", L"name_suffix", T::Text, W::Restart, "7Str", 0, 31, "Name suffix",
     "Text after a 7-string tuning name (default 7Str -> B Standard 7Str). Empty: none.", nullptr},
};
}  // namespace

const Rs7SetTable kRs7TunerSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

namespace {
bool LiveTuner(char* buf, size_t n) {
    Rs7TunerStats ts{};

    Rs7Tuner_Stats(&ts);
    std::snprintf(buf, n, "%d native session(s)", ts.sessions);

    return g_rs7_native_tuner != 0;
}

bool LiveSessions(char* buf, size_t n) {
    Rs7TunerStats ts{};

    Rs7Tuner_Stats(&ts);
    std::snprintf(buf, n, "%d native session(s) now, tuner hooks %s", ts.sessions, ts.any ? "active" : "idle");

    return true;
}

using U = Rs7UiTab;
using K = Rs7UiKind;

const Rs7UiItem kUi[] = {
    Rs7UiPlayerSection("Tuner and tuning names", "Tuner"),
    Rs7UiPlayer("tuner.lowrange", "Tuner for very low tunings",
                "Stops the wrong-instrument warning in Drop A, A Standard and G Standard."),
    Rs7UiPlayer("tuner.names", "Seven-string tuning names",
                "Song list, song pages and tuner show names such as B Standard."),
    Rs7UiPlayer("tuner.name_prefix", "Text before the tuning name",
                "For example 7Str gives 7Str B Standard. Can be empty."),
    Rs7UiPlayer("tuner.name_suffix", "Text after the tuning name",
                "For example 7Str gives B Standard 7Str. Can be empty."),
    {U::Status, K::OnOff, "7-string tuner", LiveTuner},
    {U::Tuner, K::Section, "7-string tuner"},
    {U::Tuner, K::Setting, "debug.tuner_autotune"},
    {U::Tuner, K::Row, "sessions", LiveSessions},
};
}  // namespace

const Rs7UiTable kRs7TunerUi = {kUi, int(sizeof kUi / sizeof kUi[0])};

const Rs7TunerConfig& Rs7Tuner_Config() {
    return g_config;
}

Rs7TunerConfig Rs7Tuner_ReadIni(const wchar_t* iniPath) {
    Rs7TunerConfig c;
    if (!iniPath) {
        return c;
    }

    c.tunerLowRangeRaw = Rs7Config_ReadInt(L"tuner", L"lowrange", 1, iniPath);
    c.tunerLowRange = c.tunerLowRangeRaw != 0;
    const int at = Rs7Config_ReadInt(L"debug", L"tuner_autotune", 0, iniPath, 30);

    c.tunerAutotune = at < 0 ? 0 : at > 30 ? 30 : at;
    c.tunerNamesRaw = Rs7Config_ReadInt(L"tuner", L"names", 1, iniPath);
    c.tunerNames = c.tunerNamesRaw != 0;

    // A present but empty value is a choice (no prefix / no suffix), so missing != empty here.
    for (int i = 0; i < 2; ++i) {
        std::wstring w;
        if (!Rs7Config_ReadPresent(L"tuner", i ? L"name_suffix" : L"name_prefix", iniPath, &w)) {
            continue;  // missing: the default
        }

        if (!Rs7_ValidNameAffix(w)) {
            c.nameAffixValid = false;
            continue;
        }

        std::string ascii;  // Rs7_ValidNameAffix: printable ASCII only

        for (wchar_t ch : w) {
            ascii.push_back(char(ch));
        }

        (i ? c.nameSuffix : c.namePrefix) = ascii;
    }

    return c;
}
