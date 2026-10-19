// The virtual guitarist's entry points: the SEH-guarded C entries of the four site hooks (TICK, JUD1, JUD2, AUD1:
// hooks_mid.cpp), and startup with the [test] ini keys.
#include "features/vguit/hooks.h"
#include "model/eight.h"
#include "platform/config.h"
#include "platform/log.h"
#include <share.h>

extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_on = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_slotmap = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_vg_dimgr = 0;

namespace rs7vg {

State g_vg;

Config LoadConfig(const wchar_t* ini) {
    Config c;

    if (g_rs7_eight) {
        c.stringMask = 0xFF;
    }
    c.on = Rs7Config_ReadInt(L"test", L"vguitar", 0, ini) != 0;
    wchar_t buf[128];

    GetPrivateProfileStringW(L"test", L"vguitar_mode", L"", buf, 128, ini);
    c.modeRaw = buf;
    if (!c.modeRaw.empty()) {
        c.modeValid = ParseMode(buf, &c.mode, &c.midi);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_filter", L"", buf, 128, ini);
    c.filterRaw = buf;
    if (!c.filterRaw.empty()) {
        c.filterValid = ParseFilter(buf, &c.stringMask, &c.fret, &c.filterAll);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_lead_ms", L"", buf, 128, ini);
    c.leadRaw = buf;
    if (!c.leadRaw.empty()) {
        c.leadValid = ParseFloat(buf, -500, 500, &c.leadMs);
    }
    GetPrivateProfileStringW(L"test", L"vguitar_gain", L"", buf, 128, ini);
    c.gainRaw = buf;
    if (!c.gainRaw.empty()) {
        c.gainValid = ParseFloat(buf, 0, 1, &c.gain);
    }
    c.mix = Rs7Config_ReadInt(L"test", L"vguitar_mix", 0, ini) != 0;
    c.trace = Rs7Config_ReadInt(L"test", L"vguitar_trace", 0, ini) != 0;
    GetPrivateProfileStringW(L"test", L"vguitar_chord6", L"", buf, 128, ini);
    c.chord6Raw = buf;
    if (!c.chord6Raw.empty()) {
        c.chord6Valid = ParseChord6(buf, &c.chord6, &c.chord6Shift);
    }

    return c;
}

}  // namespace rs7vg

using namespace rs7vg;

void Rs7Vg_StartFromIni(const wchar_t* iniPath) {
    const std::wstring ini = iniPath;
    Config& cfg = g_vg.cfg;

    InitializeCriticalSection(&g_vg.cs);
    cfg = LoadConfig(iniPath);
    g_vg.csvPath = ini.substr(0, ini.find_last_of(L"\\/") + 1) + L"rs7string_vguitar.csv";
    g_vg.iniPath = ini;

    if (!cfg.on) {
        Rs7Log("config: [test] vguitar=0 -> virtual guitarist off (TICK/JUD1/JUD2/AUD1 resolved, not patched)");
        return;
    }

    g_vg.csv = _wfsopen(g_vg.csvPath.c_str(), L"w", _SH_DENYWR);
    if (g_vg.csv) {
        CsvWrite(CsvHeader());
    }
    Rs7Log(
        "config: [test] vguitar=1 -> VIRTUAL GUITARIST ON: mode %s%s%s, filter %ls%s, lead %.1f ms%s, gain %.2f%s, %s; "
        "CSV %ls%s",
        ModeName(cfg.mode), cfg.mode == Mode::Midi ? (" " + std::to_string(cfg.midi)).c_str() : "",
        cfg.modeValid ? "" : " (INVALID vguitar_mode, default chart)", cfg.filterAll ? L"all" : cfg.filterRaw.c_str(),
        cfg.filterValid ? "" : " (INVALID vguitar_filter, default all)", double(cfg.leadMs),
        cfg.leadValid ? "" : " (INVALID, 0)", double(cfg.gain), cfg.gainValid ? "" : " (INVALID, 0.3)",
        cfg.mix ? "mix" : "replace", g_vg.csvPath.c_str(), g_vg.csv ? "" : " (could not open)");
    if (cfg.trace) {
        Rs7Log("config: [test] vguitar_trace=1 -> detector frame trace for 0.6 s after each onset (diagnostic)");
    }
    g_rs7_vg_on = 1;
    HANDLE h = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);

    if (h) {
        CloseHandle(h);
    }
}

bool Rs7Vg_Enabled() {
    return g_vg.cfg.on;
}

void Rs7Vg_OnSongTuning(void* session, const int16_t* tuning, int count) {
    if (!g_vg.cfg.on) {
        return;
    }

    Tuning& t = g_vg.tuning;

    g_vg.tuneSession = session;
    t.count = count < 0 ? 0 : count > 8 ? 8 : count;

    for (int i = 0; i < 8; ++i) {
        t.v[i] = i < t.count ? tuning[i] : 0;
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnTick(void* session, float songTime) {
    __try {
        TickImpl(session, songTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in TICK - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnJudge(void* note, void* session) {
    __try {
        JudgeImpl(note, session);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in JUD1 - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnScore(int hit, void* session, void* note) {
    __try {
        ScoreImpl(hit, session, note);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in JUD2 - virtual guitarist switched off");
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7Vg_OnInput(void* device, void* akBuffer) {
    __try {
        InputImpl(device, akBuffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rs7_vg_on = 0;
        Rs7Log("vguitar: FAULT in AUD1 - virtual guitarist switched off");
    }
}
