#pragma once
#include <cstddef>
#include <cstdint>

// Native publication (docs/ARCHITECTURE.md sections 3 and 4): the half of the native 7-string unit that makes the
// stock code follow a guitar built with 7/8 strings (C-lite builds it). The public header: the only one code outside
// src/features/publish/ includes.
//
// Requested with C-lite (native PegSet and tuner) and with all five native layouts present (strings, fretgrid,
// nextgrid, pitch, colours), the feature
//  - runs three preparation functions as relocated copies that read InstrumentInfo +0Bh (PUBCL PUBTO PUBWU;
//    hooks.cpp builds them, hooks_entries.cpp dispatches, hooks_callin.cpp holds the calls
//    written into them),
//  - makes the FretGrid / GuitarStrings code follow the count: bounds, the build of cells 6/7 with its bounded wait,
//    cell positions, attach points, finger span, edge string (PUBG*, PUBEDGE, R1; hooks_grid.cpp,
//    hooks_gridsites.cpp),
//  - reads the eight-entry tuning tail in three pitch readers (PUBNL PUBAN PUBAC, PUBZ*; hooks_pitch.cpp,
//    hooks_entries.cpp, hooks_stubs_pitch.cpp),
//  - widens the song's detector at song setup (S4, hooks_song.cpp).
// Logic: publish.cpp (prerequisites, classification), grid.cpp (ranks, finger span, cells, the wait table), song.cpp
// (count, lowest open note). registration.cpp holds the hook table (group Publish). Native tests: tests/publish/.

struct Rs7HookTable;
struct Rs7Int16Vec;

extern "C" {
// Requested and all prerequisites met (the ports are built and their sites written).
__declspec(dllexport) extern uintptr_t g_rs7_publish_ports;
// The ports are on and publication is ready: a guitar shows the string count it was built with.
__declspec(dllexport) extern uintptr_t g_rs7_native_publish;
// [debug] publish_trace=1: GridBuild logs its not-ready reason (rate limited 1/s, 100 lines); the LS* probes.
__declspec(dllexport) extern uintptr_t g_rs7_publish_trace;
// Review F3: 1 while the guitar's GridBuild timed out (a 7/8-built guitar whose neck has no cells 6/7): the song's
// detector is not widened. Cleared when its grid builds, and for every guitar by a clite rebuild.
__declspec(dllexport) int __cdecl Rs7Publish_NeckFailed(const void* guitar);
__declspec(dllexport) void __cdecl Rs7Publish_GridForgetAll();
// The TU8A copy of StringAction (features/tuner) contains the PUBEDGE site: its edge string over the native count.
__declspec(dllexport) unsigned __cdecl Rs7Publish_EdgeCopy(uint8_t* info, unsigned first);
// S4 (features/pitch/hooks_mode.cpp), game thread. Native 7-string mode only (g_rs7_native_publish). A 7/8-entry guitar
// tuning on a guitar that was built with that many strings: detector +13D8h = count and +11F8h = the lowest open MIDI
// over all strings; the count survives the SetTuning that S4 runs after it (Rs7Pitch_SongCount). Any other
// song (6 strings, bass, a guitar built with another count, detector without the native tail) is left at the
// stock width and logged.
__declspec(dllexport) void __cdecl Rs7Song_WidenDetector(void* session, void* detector, const Rs7Int16Vec* tuning);
}

// Startup (hooks.cpp): [debug] publish_trace, read before any site is patched.
void Rs7Publish_ReadIni(const wchar_t* ini);
// Startup: `requested` is the C-lite mode (native PegSet and tuner); checks the prerequisites and builds the ports.
void Rs7Publish_Start(bool requested);

// The PUB* sites and R1 (group Publish), registration.cpp.
extern const Rs7HookTable kRs7PublishHooks;
// LSA LSG LSC LSE, the load-step probes (group Core).
extern const Rs7HookTable kRs7LoadProbeHooks;

// ---- logic (publish.cpp), for the tests
namespace rs7pub {
constexpr unsigned kPrerequisites = 31;  // strings, fretgrid, nextgrid, pitch, colours
unsigned Missing(unsigned mask);
bool PortsEnabled(bool requested, unsigned mask);
bool PublicationReady();
// Exact scalar form of 875240..8753D2: candidate order and early exit match the stock unroll.
// Only guitar; count must be 6, 7 or 8. Writes record +14h (fret), +18h (string) on a match.
int Classify(uint8_t* record, const int16_t* tuning, unsigned count, int midi, int initial);
}  // namespace rs7pub

// ---- logic (grid.cpp), for the tests
namespace rs7pubgrid {
unsigned PhysicalRank(unsigned string, unsigned count);
unsigned PhysicalEdge(unsigned count, bool first);
// Exact stock scan for stock templates; native T8 scans physical ranks.
int FingerSpan(const uint8_t* fingers, unsigned count, unsigned finger, int* span);
// GridBuild's bounded wait (pure, tests): one entry per grid that reported "not ready". WaitStep returns true
// (give up) once `grid` has been not ready for limitMs; WaitDone clears its entry.
// Review F3: an entry belongs to one build. A poll that finds the entry's owner (the guitar) changed, or that comes
// more than staleMs after the previous poll (the abandoned grid's address was reused), starts a fresh entry instead
// of inheriting the old deadline. `failed`: the wait timed out on a guitar built with 7/8 strings (the neck has no
// cells 6/7): Failed(owner) is what song setup asks before it widens a detector.
constexpr uint64_t kWaitStaleMs = 10000;

struct WaitEntry {
    const void* grid;
    uint64_t since;
    unsigned calls;
    unsigned reason;
    uint64_t last;
    const void* owner;
    bool failed;
};

bool WaitStep(WaitEntry* table, size_t n, const void* grid, unsigned reason, uint64_t now, uint64_t limitMs,
              const void* owner = nullptr, uint64_t staleMs = kWaitStaleMs);
void WaitDone(WaitEntry* table, size_t n, const void* grid,
              const void* owner = nullptr);  // also drops the owner's entries
void MarkFailed(WaitEntry* table, size_t n, const void* grid);
bool Failed(const WaitEntry* table, size_t n, const void* owner);
int GetCellPos(const uint8_t* grid, unsigned count, unsigned fret, unsigned k, unsigned string, float* out);
}  // namespace rs7pubgrid

// ---- logic (song.cpp), for the tests
namespace rs7pubstate {
// A validated tuning vector determines the instrument's width. Unsupported guitar widths fail
// closed to six; bass always retains its stock four strings.
unsigned Count(size_t tuningCount, bool bass);
int Lowest(const int16_t tuning[8], unsigned count, bool bass);
}  // namespace rs7pubstate
