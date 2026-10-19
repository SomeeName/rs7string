#include <windows.h>
#include <cstdlib>
#include <new>
#include <cstdio>
static int failArray = -1;
void* operator new[](size_t n) {
    if (failArray == 0) { failArray = -1; throw std::bad_alloc(); }
    if (failArray > 0) --failArray;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
#include "features/nslot/records.cpp"
#include "features/nslot/hooks.cpp"
extern "C" uintptr_t g_rs7_diagrams = 0, g_rs7_publish_ports = 0, g_rs7_eight = 0;
void Rs7Log(const char*, ...) {}
void Rs7Alloc_Inc(Rs7AllocKind) {}
void Rs7Alloc_Dec(Rs7AllocKind) {}
void Rs7Diagram_Drop(void*) {}
std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void*) { return {}; }
int rs7ext::Block::Column(int) const { return -1; }
bool rs7ext::Block::TemplateColumn(uint32_t, int, int*, int*) const { return false; }
int main(int argc, char**) {
    uint8_t song[0x120] = {}, tpl[rs7ns::kTplSize] = {}, cn[rs7ns::kCnSize] = {};
    uintptr_t tv[2] = {reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl)};
    uintptr_t cv[2] = {reinterpret_cast<uintptr_t>(cn), reinterpret_cast<uintptr_t>(cn + sizeof cn)};
    std::memcpy(song + 0x94, tv, sizeof tv);
    std::memcpy(song + 0xAC, cv, sizeof cv);
    if (argc > 1) {
        Rs7Ns_OnSngLoaded(song);
        failArray = 1; // BuildTemplates succeeds, the C8 scratch allocation fails.
        Rs7Ns_OnPostProcessed(song);
    } else {
        failArray = 0;
        Rs7Ns_OnSngLoaded(song);
    }
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        std::puts("FAIL: allocation failure left nslot lock held");
        return 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    Rs7Ns_OnSngLoaded(song);
    Rs7Ns_OnPostProcessed(song);
    if (!g_rs7_ns[0].tplBegin || !g_rs7_ns[0].cnBegin) return 2;
    Rs7Ns_OnSongFree(song);
    std::puts("PASS: nslot allocation failure releases lock and next load succeeds");
}
