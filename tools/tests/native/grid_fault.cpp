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
extern "C" int Rs7Gs_ExtraReady(uint8_t*) { return 0; }
extern "C" int Rs7Gs_ExtraWhy(uint8_t*) { return 1; }
int rs7fg::FillExtraCells(uint8_t*, uint32_t) { return 0; }
int main() {
    uint8_t grid[16] = {}, guitar[0x30] = {}, info[0x44] = {}, gs[16] = {};
    *reinterpret_cast<uint8_t**>(guitar + 0x24) = info;
    *reinterpret_cast<uint8_t**>(guitar + 0xC) = gs;
    // A fault after a prior ordinary wait shares its original deadline.
    nowMs = 1000;
    if (NotReady(grid, 2) != 1) return 1;
    nowMs = 5999;
    if (Rs7Publish_GridBuild(grid, nullptr) != 1) return 2;
    nowMs = 6000;
    if (Rs7Publish_GridBuild(grid, nullptr) != 2) {
        std::puts("FAIL: persistent GridBuild fault bypasses shared 5s deadline"); return 4;
    }
    nowMs = 6001;
    if (Rs7Publish_GridBuild(grid, nullptr) != 2) return 5;
    std::puts("PASS: GridBuild faults share the bounded fallback");
}
