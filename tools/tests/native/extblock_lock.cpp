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
#include "features/nslot/hooks_extblock.cpp"
void Rs7Log(const char*, ...) {}
extern "C" void __cdecl Rs7Mid_Fldcw(uint32_t) {}  // midhook_thunks.asm (Rs7Mid::GameFcw in XB1)
void Rs7Ns_OnSngLoaded(void*) {}
void Rs7Ns_OnSongFree(void*) {}
bool rs7chord::FromTemplateBytes(const uint8_t*, Ext*) { return false; }
int main() {
    auto block = std::make_shared<rs7ext::Block>();
    int song = 0;
    failNext = true; // The store's first vector insertion must allocate.
    try { Rs7Ext_Store(&song, block); } catch (...) {}
    if (failNext) { std::puts("FAIL: fault injection did not allocate"); return 2; }
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        std::puts("FAIL: allocation failure left extblock lock held");
        return 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    Rs7Ext_Store(&song, block);
    if (Rs7Ext_Find(&song) != block) return 3;
    Rs7Ext_Store(&song, nullptr);
    std::puts("PASS: extblock allocation failure releases lock and next store succeeds");
}
