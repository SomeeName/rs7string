#include <windows.h>
#include <cstdio>
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
void Vector(uint8_t* song, size_t off, uintptr_t begin, uintptr_t end) {
    uintptr_t values[2] = {begin, end}; std::memcpy(song + off, values, sizeof values);
}
int main(int argc, char**) {
    uint8_t song[0x120] = {};
    if (argc == 1) {
        // These stock vectors fit a 32-bit address span; their expanded arrays do not.
        for (int type : {0,1}) {
            uint32_t source = type ? rsxrns::kCnSize : rsxrns::kTplSize;
            uint32_t dest = type ? rsxrns::kC8Size : rsxrns::kT8Size;
            size_t offset = type ? 0xAC : 0x94;
            uintptr_t end = 0x1000 + (SIZE_MAX / dest + 1) * source;
            Vector(song, offset, 0x1000, end);
            SongView v;
            if (ReadSong(song, &v)) { std::printf("FAIL: expanded %s byte count wraps\n", type ? "C8" : "T8"); return 1; }
            std::memset(song, 0, sizeof song);
        }
    } else {
        SYSTEM_INFO info; GetSystemInfo(&info);
        uint8_t* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!pages) return 2;
        DWORD old; VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old);
        uint8_t tpl[rsxrns::kTplSize] = {};
        Vector(song, 0x94, reinterpret_cast<uintptr_t>(tpl), reinterpret_cast<uintptr_t>(tpl + sizeof tpl));
        uintptr_t cn = reinterpret_cast<uintptr_t>(pages + info.dwPageSize - rsxrns::kCnSize);
        Vector(song, 0xAC, cn, cn + 2 * rsxrns::kCnSize);
        RsxrNs_OnSngLoaded(song); RsxrNs_OnPostProcessed(song);
        if (g_rsxr_ns[0].cnBegin) { std::puts("FAIL: unreadable second ChordNotes record published incomplete C8 array"); return 3; }
        RsxrNs_OnSongFree(song); VirtualFree(pages, 0, MEM_RELEASE);
    }
    std::puts("PASS: malformed vector rejected without publishing incomplete arrays");
}
