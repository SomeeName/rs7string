#include "core/allocstats.h"
#include "core/assetload.h"
#include "core/config.h"
#include "core/diagrams.h"
#include "core/extblock.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/nslot.h"
#include "core/rampstate.h"
#include "core/sidestore.h"
#include "core/string7.h"
#include "core/tuner.h"
#include "d3d/swap.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <psapi.h>

namespace {
volatile LONG g_live[kAkCount] = {};
volatile LONG g_total[kAkCount] = {};
volatile LONG g_enabled = 0;
volatile LONG g_songs = 0;
const char* const kNames[kAkCount] = {"gs",       "strobj",   "info",   "t8",       "c8",
                                      "diagfile", "headslot", "merges", "textures", "bindalt"};
}  // namespace

void Rs7Alloc_Configure(const wchar_t* iniPath) {
    const bool on = iniPath && Rs7Config_ReadInt(L"debug", L"alloc_stats", 0, iniPath) != 0;

    InterlockedExchange(&g_enabled, on ? 1 : 0);
    Rs7Log("config: [debug] alloc_stats=%d -> %s", int(on),
           on ? "live/total counts of the DLL's allocations are logged at song start and song end (allocstats lines)"
              : "off");
}

bool Rs7Alloc_Enabled() {
    return g_enabled != 0;
}

void Rs7Alloc_SetEnabledForTests(bool on) {
    InterlockedExchange(&g_enabled, on ? 1 : 0);
}

void Rs7Alloc_Inc(Rs7AllocKind k) {
    if (k < 0 || k >= kAkCount) {
        return;
    }

    InterlockedIncrement(&g_live[k]);
    InterlockedIncrement(&g_total[k]);
}

void Rs7Alloc_Dec(Rs7AllocKind k) {
    if (k >= 0 && k < kAkCount) {
        InterlockedDecrement(&g_live[k]);
    }
}

long Rs7Alloc_Live(Rs7AllocKind k) {
    return k >= 0 && k < kAkCount ? g_live[k] : 0;
}

long Rs7Alloc_Total(Rs7AllocKind k) {
    return k >= 0 && k < kAkCount ? g_total[k] : 0;
}

const char* Rs7Alloc_Name(Rs7AllocKind k) {
    return k >= 0 && k < kAkCount ? kNames[k] : "?";
}

void Rs7Alloc_ResetForTests() {
    for (int i = 0; i < kAkCount; ++i) {
        g_live[i] = g_total[i] = 0;
    }

    g_songs = 0;
}

std::string Rs7Alloc_Line(const char* when, int song) {
    char buf[1400];
    int n = std::snprintf(buf, sizeof buf, "allocstats %s #%d:", when, song);
    auto add = [&](const char* fmt, auto... a) {
        if (n > 0 && size_t(n) < sizeof buf) {
            n += std::snprintf(buf + n, sizeof buf - size_t(n), fmt, a...);
        }
    };
    PROCESS_MEMORY_COUNTERS_EX pmc{};

    pmc.cb = sizeof pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        add(" priv=%.1fMB ws=%.1fMB", double(pmc.PrivateUsage) / 1048576.0, double(pmc.WorkingSetSize) / 1048576.0);
    }
    MEMORYSTATUSEX ms{};

    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        add(" freeVA=%.0fMB", double(ms.ullAvailVirtual) / 1048576.0);
    }
    DWORD handles = 0;

    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        add(" handles=%lu", handles);
    }
    add(" |");

    for (int i = 0; i < kAkCount; ++i) {
        add(" %s=%ld/%ld", kNames[i], g_live[i], g_total[i]);  // live/total
    }

    Rs7GsStats gs{};

    Rs7Gs_Stats(&gs);
    Rs7S7Stats s7{};

    Rs7S7_Stats(&s7);
    Rs7HeadStats hs{};

    Rs7Head_Stats(&hs);
    Rs7TunerStats tn{};

    Rs7Tuner_Stats(&tn);
    Rs7SwapStats sw{};

    Rs7Swap_Stats(&sw);
    int candLive = 0;

    for (int i = 0; i < Rs7Cand_Count(); ++i) {
        const Rs7Cand* c = Rs7Cand_At(i);

        if (c && c->state != Rs7CandState::Retired) {
            ++candLive;
        }
    }

    add(" | gsNative=%d s7Tab=%d s7Modes=%d pegCtors=%d tunerSess=%d nsSongs=%d extBlocks=%d diagBind=%d side=%d "
        "rampCands=%d rampCopies=%u rampActive=%u",
        gs.native, s7.guitars, s7.sevenGuitars, hs.pegsets, tn.sessions, Rs7Ns_LiveSongs(), Rs7Ext_StoredBlocks(),
        Rs7Diagram_Bindings(), Rs7Store_Count(), candLive, sw.copies, sw.active);

    return std::string(buf);
}

void Rs7Alloc_Snapshot(const char* when) {
    if (!g_enabled) {
        return;
    }

    try {
        const int song = std::strcmp(when, "song start") == 0 ? int(InterlockedIncrement(&g_songs)) : int(g_songs);

        Rs7Log("%s", Rs7Alloc_Line(when, song).c_str());
    } catch (...) {
    }
}
