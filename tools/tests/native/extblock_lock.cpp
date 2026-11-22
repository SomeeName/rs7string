#include <windows.h>
#include <cstdlib>
#include <new>
#include <cstdio>
static bool failNext = false;
void* operator new(size_t n) {
    if (failNext) { failNext = false; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
#include "model/extblock.cpp"
#include "platform/safecopy.cpp"  // Rsxr_SafeCopy* (the shared safe-copy helpers)
#include "features/nslot/hooks_extblock.cpp"
void RsxrLog(const char*, ...) {}
extern "C" void __cdecl RsxrMid_Fldcw(uint32_t) {}  // midhook_thunks.asm (RsxrMid::GameFcw in XB1)
void RsxrNs_OnSngLoaded(void*) {}
void RsxrNs_OnSongFree(void*) {}
int main() {
    auto block = std::make_shared<rsxrext::Block>();
    int song = 0;
    failNext = true; // The store's first vector insertion must allocate.
    try { RsxrExt_Store(&song, block); } catch (...) {}
    if (failNext) { std::puts("FAIL: fault injection did not allocate"); return 2; }
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        std::puts("FAIL: allocation failure left extblock lock held");
        return 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    RsxrExt_Store(&song, block);
    if (RsxrExt_Find(&song) != block) return 3;
    RsxrExt_Store(&song, nullptr);
    std::puts("PASS: extblock allocation failure releases lock and next store succeeds");
}
