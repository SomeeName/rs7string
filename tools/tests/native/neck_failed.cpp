// Review F3: GridBuild's 5 s timeout on a guitar built with 7/8 strings marks its neck FAILED (no silent completion
// that song setup would widen a detector for), and the wait bookkeeping does not leak into another build that reuses
// the grid's address. Test clock, no Win32 time.
#include <cstdint>
#include <windows.h>
#include <cstdio>
static uint64_t nowMs = 0;
static uint64_t TestClock() { return nowMs; }
#define GetTickCount64 TestClock
#include "core/publish_grid.cpp"
#undef GetTickCount64
extern "C" uintptr_t g_rs7_native_fretgrid = 1, g_rs7_fg_row = rs7fg::kRowNative;
extern "C" uintptr_t g_rs7_publish_ports = 1;
void Rs7Log(const char*, ...) {}
const uint8_t* Rs7Ns_AsT8(const void*) { return nullptr; }
extern "C" int Rs7Gs_ExtraReady(uint8_t*) { return 0; }   // never ready: the wait runs into its deadline
extern "C" int Rs7Gs_ExtraWhy(uint8_t*) { return 1; }
int rs7fg::FillExtraCells(uint8_t*, uint32_t) { return 0; }

#define EXPECT(c, code) do { if (!(c)) { std::printf("FAIL %d: %s\n", code, #c); return code; } } while (0)

int main() {
    uint8_t gridA[16] = {}, gridB[16] = {};
    uint8_t guitar7[0x30] = {}, guitar6[0x30] = {}, info7[0x44] = {}, info6[0x44] = {}, gs[16] = {};
    info7[0xB] = 7; info6[0xB] = 6;
    *reinterpret_cast<uint8_t**>(guitar7 + 0x24) = info7;
    *reinterpret_cast<uint8_t**>(guitar7 + 0xC) = gs;
    *reinterpret_cast<uint8_t**>(guitar6 + 0x24) = info6;
    *reinterpret_cast<uint8_t**>(guitar6 + 0xC) = gs;

    // 1. The timeout path on a 7-built guitar: waits, gives up with the stock completion, marks the neck failed.
    *reinterpret_cast<uint8_t**>(gridA) = guitar7;
    nowMs = 1000;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 1, 1);
    EXPECT(!Rs7Publish_NeckFailed(guitar7), 2);
    nowMs = 5999;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 1, 3);
    EXPECT(!Rs7Publish_NeckFailed(guitar7), 4);
    nowMs = 6000;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 2, 5);   // the load still completes (never held forever) ...
    EXPECT(Rs7Publish_NeckFailed(guitar7), 6);              // ... but song setup is told the neck failed
    EXPECT(!Rs7Publish_NeckFailed(guitar6), 7);

    // 2. A 6-built guitar timing out is the old behaviour: stock completion, nothing failed.
    *reinterpret_cast<uint8_t**>(gridB) = guitar6;
    nowMs = 7000;
    EXPECT(Rs7Publish_GridBuild(gridB, guitar6) == 1, 8);
    nowMs = 12000;
    EXPECT(Rs7Publish_GridBuild(gridB, guitar6) == 2, 9);
    EXPECT(!Rs7Publish_NeckFailed(guitar6), 10);

    // 3. Address reuse: the abandoned, timed-out grid A is followed 90 s later by a new grid at the same address
    //    (same guitar address too). It gets a fresh deadline and no failed mark.
    nowMs = 100000;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 1, 11);   // not "give up at once"
    EXPECT(!Rs7Publish_NeckFailed(guitar7), 12);
    nowMs = 104999;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 1, 13);
    nowMs = 105000;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7) == 2, 14);   // its own 5 s
    EXPECT(Rs7Publish_NeckFailed(guitar7), 15);

    // 4. Another owner at the same grid address (no long gap): also a fresh entry.
    uint8_t guitar7b[0x30] = {};
    *reinterpret_cast<uint8_t**>(guitar7b + 0x24) = info7;
    *reinterpret_cast<uint8_t**>(guitar7b + 0xC) = gs;
    *reinterpret_cast<uint8_t**>(gridA) = guitar7b;
    nowMs = 105100;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7b) == 1, 16);
    EXPECT(!Rs7Publish_NeckFailed(guitar7b), 17);

    // 5. A rebuild (GKT) forgets everything.
    nowMs = 111000;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7b) == 2, 18);
    EXPECT(Rs7Publish_NeckFailed(guitar7b), 19);
    Rs7Publish_GridForgetAll();
    EXPECT(!Rs7Publish_NeckFailed(guitar7b), 20);
    nowMs = 111001;
    EXPECT(Rs7Publish_GridBuild(gridA, guitar7b) == 1, 21);   // waiting again from zero
    std::puts("PASS: a 7/8-built guitar's GridBuild timeout marks the neck failed; the wait does not outlive its build");
}
