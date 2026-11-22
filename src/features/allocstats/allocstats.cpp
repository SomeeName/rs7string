#include "features/allocstats/allocstats.h"
#include "features/colours/colours.h"
#include "features/diagrams/diagrams.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "features/tuner/tuner.h"
#include "platform/config.h"
#include "platform/log.h"
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

void RsxrAlloc_Configure(const wchar_t* iniPath) {
    const bool on = iniPath && RsxrConfig_ReadInt(L"debug", L"alloc_stats", 0, iniPath) != 0;

    InterlockedExchange(&g_enabled, on ? 1 : 0);
    RsxrLog("config: [debug] alloc_stats=%d -> %s", int(on),
            on ? "live/total counts of the DLL's allocations are logged at song start and song end (allocstats lines)"
               : "off");
}

void RsxrAlloc_SetEnabledForTests(bool on) {
    InterlockedExchange(&g_enabled, on ? 1 : 0);
}

void RsxrAlloc_Inc(RsxrAllocKind k) {
    if (k < 0 || k >= kAkCount) {
        return;
    }

    InterlockedIncrement(&g_live[k]);
    InterlockedIncrement(&g_total[k]);
}

void RsxrAlloc_Dec(RsxrAllocKind k) {
    if (k >= 0 && k < kAkCount) {
        InterlockedDecrement(&g_live[k]);
    }
}

long RsxrAlloc_Live(RsxrAllocKind k) {
    return k >= 0 && k < kAkCount ? g_live[k] : 0;
}

long RsxrAlloc_Total(RsxrAllocKind k) {
    return k >= 0 && k < kAkCount ? g_total[k] : 0;
}

const char* RsxrAlloc_Name(RsxrAllocKind k) {
    return k >= 0 && k < kAkCount ? kNames[k] : "?";
}

void RsxrAlloc_ResetForTests() {
    for (int i = 0; i < kAkCount; ++i) {
        g_live[i] = g_total[i] = 0;
    }

    g_songs = 0;
}

std::string RsxrAlloc_Line(const char* when, int song) {
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

    RsxrGsStats gs{};

    RsxrGs_Stats(&gs);
    RsxrS7Stats s7{};

    RsxrS7_Stats(&s7);
    RsxrHeadStats hs{};

    RsxrHead_Stats(&hs);
    RsxrTunerStats tn{};

    RsxrTuner_Stats(&tn);
    RsxrSwapStats sw{};

    RsxrSwap_Stats(&sw);
    int candLive = 0;

    for (int i = 0; i < RsxrCand_Count(); ++i) {
        const RsxrCand* c = RsxrCand_At(i);

        if (c && c->state != RsxrCandState::Retired) {
            ++candLive;
        }
    }

    add(" | gsNative=%d s7Tab=%d s7Modes=%d pegCtors=%d tunerSess=%d nsSongs=%d extBlocks=%d diagBind=%d side=%d "
        "rampCands=%d rampCopies=%u rampActive=%u",
        gs.native, s7.guitars, s7.sevenGuitars, hs.pegsets, tn.sessions, RsxrNs_LiveSongs(), RsxrExt_StoredBlocks(),
        RsxrDiagram_Bindings(), RsxrStore_Count(), candLive, sw.copies, sw.active);

    return std::string(buf);
}

void RsxrAlloc_Snapshot(const char* when) {
    if (!g_enabled) {
        return;
    }

    try {
        const int song = std::strcmp(when, "song start") == 0 ? int(InterlockedIncrement(&g_songs))
                                                              : int(InterlockedCompareExchange(&g_songs, 0, 0));

        RsxrLog("%s", RsxrAlloc_Line(when, song).c_str());
    } catch (...) {
    }
}
