#pragma once
#include "core/publish.h"
#include <cstddef>
#include <cstdint>

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

extern "C" {
// [debug] publish_trace=1: GridBuild logs its not-ready reason (rate limited 1/s, 100 lines).
__declspec(dllexport) extern uintptr_t g_rs7_publish_trace;
// GridBuild's per-grid wait table (8 entries), exported for the tests.
__declspec(dllexport) extern rs7pubgrid::WaitEntry g_rs7_publish_wait[8];
__declspec(dllexport) int __cdecl Rs7Publish_GridWait(uint8_t* grid, unsigned reason);
// Review F3: 1 while the guitar's GridBuild timed out (a 7/8-built guitar whose neck has no cells 6/7): the song's
// detector is not widened. Cleared when its grid builds, and for every guitar by a clite rebuild.
__declspec(dllexport) int __cdecl Rs7Publish_NeckFailed(const void* guitar);
__declspec(dllexport) void __cdecl Rs7Publish_GridForgetAll();
__declspec(dllexport) int __cdecl Rs7Publish_GridPos(uint8_t* grid, unsigned fret, unsigned k, unsigned string,
                                                     float* out);
__declspec(dllexport) int __cdecl Rs7Publish_GridBuild(uint8_t* grid, uint8_t* guitar);
__declspec(dllexport) int __cdecl Rs7Publish_FingerSpan(const uint8_t* tpl, unsigned finger, int* span);
__declspec(dllexport) unsigned __cdecl Rs7Publish_EdgeCopy(uint8_t* info, unsigned first);
__declspec(dllexport) unsigned __cdecl Rs7Publish_Edge(uint8_t* info, unsigned first);
__declspec(dllexport) void Rs7_Stub_PUBGP();
}
