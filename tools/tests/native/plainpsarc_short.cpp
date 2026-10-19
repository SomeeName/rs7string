#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "features/psarc/psarc.cpp"
uintptr_t g_rs7_target_PP_SNGD = 0;
#include "sngstub.inc"
bool Short(const uint8_t* src, uint32_t len) {
    __try { return !Rs7_Stub_PP_SNGD(src, len, nullptr, 0, nullptr, 0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool __cdecl Decrypt(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t) { return true; }
int main() {
    if (!Short(nullptr, 0)) { std::puts("FAIL: empty input faults before length rejection"); return 1; }
    SYSTEM_INFO info; GetSystemInfo(&info);
    uint8_t* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!pages) return 2;
    DWORD old; VirtualProtect(pages, info.dwPageSize, PAGE_NOACCESS, &old);
    const uint8_t* src = pages + info.dwPageSize;
    for (uint32_t n = 0; n < 16; ++n) if (!Short(src, n)) { std::puts("FAIL: short input reads inaccessible preceding header"); return 3; }
    uint8_t sng[28] = {0x4a,0,0,0,1,0,0,0};
    sng[24] = 1; sng[25] = 2; sng[26] = 3; sng[27] = 4;
    uint8_t out[4] = {};
    if (!Rs7_Stub_PP_SNGD(sng + 8, 20, nullptr, 0, out, 4) || std::memcmp(out,sng+24,4)) return 4;
    if (Rs7_Stub_PP_SNGD(sng + 8, 20, nullptr, 0, out, 3)) return 5;
    sng[4] = 3; g_rs7_target_PP_SNGD = reinterpret_cast<uintptr_t>(&Decrypt);
    if (!Rs7_Stub_PP_SNGD(sng + 8, 20, nullptr, 0, out, 4)) return 6;
    VirtualFree(pages, 0, MEM_RELEASE);
    std::puts("PASS: short inputs rejected before header reads; copy/capacity/decrypt routing preserved");
}
