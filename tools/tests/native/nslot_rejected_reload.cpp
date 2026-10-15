#include <windows.h>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstring>
static int failArray = -1;
static bool failHeap = false;
void* operator new[](size_t n) {
    if (failArray == 0) { failArray = -1; throw std::bad_alloc(); }
    if (failArray > 0) --failArray;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void* TestHeapAlloc(HANDLE heap, DWORD flags, SIZE_T n) {
    if (failHeap) { failHeap = false; return nullptr; }
    return HeapAlloc(heap, flags, n);
}
#define HeapAlloc TestHeapAlloc
#include "core/nslot.cpp"
#undef HeapAlloc
extern "C" uintptr_t g_rs7_diagrams = 0, g_rs7_publish_ports = 0, g_rs7_eight = 0;
static int arrays = 0;
void Rs7Log(const char*, ...) {}
void Rs7Alloc_Inc(Rs7AllocKind) { ++arrays; }
void Rs7Alloc_Dec(Rs7AllocKind) { --arrays; }
void Rs7Diagram_Drop(void*) {}
std::shared_ptr<const rs7ext::Block> Rs7Ext_Find(const void*) { return {}; }
int rs7ext::Block::Column(int) const { return -1; }
bool rs7ext::Block::TemplateColumn(uint32_t, int, int*, int*) const { return false; }
void Vector(uint8_t* song, size_t off, uintptr_t begin, uintptr_t end) {
    uintptr_t values[2] = {begin, end}; std::memcpy(song + off, values, sizeof values);
}
int main(int argc, char** argv) {
    if (argc != 2) return 9;
    const char* mode = argv[1];
    uint8_t song[0x120] = {}, tpl[rs7ns::kTplSize] = {}, cn[rs7ns::kCnSize] = {};
    auto restore = [&]() {
        Vector(song, 0x94, reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl));
        Vector(song, 0xAC, reinterpret_cast<uintptr_t>(cn), reinterpret_cast<uintptr_t>(cn + sizeof cn));
    };
    restore(); Rs7Ns_OnSngLoaded(song); Rs7Ns_OnPostProcessed(song);
    if (!g_rs7_ns[0].tplBegin || !g_rs7_ns[0].cnBegin || arrays != 2) return 8;
    bool reload = !std::strcmp(mode, "reload");
    if (reload || !std::strcmp(mode, "partial"))
        Vector(song, 0x94, reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl - 1));
    else if (!std::strcmp(mode, "template-copy")) Vector(song, 0x94, 1, 1 + rs7ns::kTplSize);
    else if (!std::strcmp(mode, "record-copy")) Vector(song, 0xAC, 1, 1 + rs7ns::kCnSize);
    else if (!std::strcmp(mode, "template-allocation")) failArray = 0;
    else if (!std::strcmp(mode, "scratch-allocation")) failArray = 1;
    else if (!std::strcmp(mode, "c8-allocation")) failHeap = true;
    else return 7;
    if (reload) Rs7Ns_OnSngLoaded(song); else Rs7Ns_OnPostProcessed(song);
    const Rs7NsEntry& e = g_rs7_ns[0];
    if (e.tplBegin || e.cnBegin || e.t8 || e.c8 || e.tplCount || e.cnCount || arrays) {
        std::printf("FAIL: %s retained published or allocated arrays\n", mode); return 1;
    }
    if (reload ? e.song != 0 : (e.song != reinterpret_cast<uintptr_t>(song) || e.origCn != 1)) return 2;
    if (!TryAcquireSRWLockExclusive(&g_lock)) { std::puts("FAIL: writer lock held"); return 3; }
    ReleaseSRWLockExclusive(&g_lock);
    restore();
    if (reload) Rs7Ns_OnSngLoaded(song);
    Rs7Ns_OnPostProcessed(song);
    if (!e.tplBegin || !e.cnBegin || arrays != 2) { std::puts("FAIL: retry failed"); return 4; }
    Rs7Ns_OnSongFree(song);
    if (arrays) return 5;
    std::printf("PASS: %s unpublishes/frees arrays, releases lock, and permits retry\n", mode);
}
