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
#include "platform/safecopy.cpp"  // Rsxr_SafeCopy* (the shared safe-copy helpers)
#include "features/nslot/records.cpp"
#include "features/nslot/hooks.cpp"
extern "C" uintptr_t g_rsxr_diagrams = 0, g_rsxr_publish_ports = 0;
void RsxrLog(const char*, ...) {}
void RsxrAlloc_Inc(RsxrAllocKind) {}
void RsxrAlloc_Dec(RsxrAllocKind) {}
void RsxrDiagram_Drop(void*) {}
std::shared_ptr<const rsxrext::Block> RsxrExt_Find(const void*) { return {}; }
int rsxrext::Block::Column(int) const { return -1; }
bool rsxrext::Block::TemplateColumn(uint32_t, int, int*, int*) const { return false; }
int main(int argc, char**) {
    uint8_t song[0x120] = {}, tpl[rsxrns::kTplSize] = {}, cn[rsxrns::kCnSize] = {};
    uintptr_t tv[2] = {reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl)};
    uintptr_t cv[2] = {reinterpret_cast<uintptr_t>(cn), reinterpret_cast<uintptr_t>(cn + sizeof cn)};
    std::memcpy(song + 0x94, tv, sizeof tv);
    std::memcpy(song + 0xAC, cv, sizeof cv);
    if (argc > 1) {
        RsxrNs_OnSngLoaded(song);
        failArray = 1; // BuildTemplates succeeds, the C8 scratch allocation fails.
        RsxrNs_OnPostProcessed(song);
    } else {
        failArray = 0;
        RsxrNs_OnSngLoaded(song);
    }
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        std::puts("FAIL: allocation failure left nslot lock held");
        return 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    RsxrNs_OnSngLoaded(song);
    RsxrNs_OnPostProcessed(song);
    if (!g_rsxr_ns[0].tplBegin || !g_rsxr_ns[0].cnBegin) return 2;
    RsxrNs_OnSongFree(song);
    std::puts("PASS: nslot allocation failure releases lock and next load succeeds");
}
