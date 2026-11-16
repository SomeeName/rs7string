// rs7menu::ReadString (the game's string at an address) and RecordCount, on fake 32-bit structs.
#include "features/arrangements/menu.h"
#include "test.h"
#include <windows.h>
#include <cstring>

namespace {
// The game string: +0 data (or the inline buffer itself), +10h end, +14h capacity end.
struct FakeString {
    alignas(4) uint32_t s[rs7menu::kStringSize / 4];
};

uint32_t Addr(const void* p) {
    return uint32_t(uintptr_t(p));
}

void SetInline(FakeString& f, const char* text) {
    const uint32_t len = uint32_t(std::strlen(text));

    std::memset(f.s, 0, sizeof f.s);
    std::memcpy(f.s, text, len);
    f.s[4] = Addr(f.s) + len;
    f.s[5] = Addr(f.s) + 0x10;
}

void SetHeap(FakeString& f, const char* heap, uint32_t len) {
    std::memset(f.s, 0, sizeof f.s);
    f.s[0] = Addr(heap);
    f.s[4] = Addr(heap) + len;
    f.s[5] = Addr(heap) + len + 16;
}
}  // namespace

TEST(menu_readstring_inline_buffer) {
    FakeString f;
    char out[32];

    std::memset(out, 'x', sizeof out);
    SetInline(f, "playSong");
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK(std::strcmp(out, "playSong") == 0);
}

TEST(menu_readstring_heap_buffer) {
    static const char kText[] = "Buttons.arrangement3.States";
    FakeString f;
    char out[64];

    SetHeap(f, kText, uint32_t(sizeof kText - 1));
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK(std::strcmp(out, kText) == 0);
}

TEST(menu_readstring_empty_string) {
    FakeString f;
    char out[8];

    std::memset(out, 'x', sizeof out);
    SetInline(f, "");
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK_EQ(out[0], 0);
}

TEST(menu_readstring_size_limit_is_below_n) {
    static const char kText[] = "abcdef";
    FakeString f;
    char out[16];

    SetHeap(f, kText, 6);

    // n == size + 1 leaves room for the terminating zero; n == size does not.
    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(Addr(&f), out, 7);
    CHECK(std::strcmp(out, "abcdef") == 0);
    CHECK_EQ(out[6], 0);
    CHECK_EQ(out[7], 'x');  // nothing written past the terminator

    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(Addr(&f), out, 6);
    CHECK_EQ(out[0], 0);  // too long: ""
    CHECK_EQ(out[1], 'x');

    rs7menu::ReadString(Addr(&f), out, 3);
    CHECK_EQ(out[0], 0);
}

TEST(menu_readstring_end_before_data_is_rejected) {
    static const char kText[] = "abcdef";
    FakeString f;
    char out[16];

    SetHeap(f, kText, 3);
    f.s[4] = Addr(kText) - 4;  // end pointer below the data pointer
    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 'x');
}

TEST(menu_readstring_unreadable_address_gives_empty) {
    char out[8];

    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(0, out, sizeof out);
    CHECK_EQ(out[0], 0);
}

TEST(menu_readstring_unreadable_heap_gives_empty) {
    FakeString f;
    char out[8];

    std::memset(f.s, 0, sizeof f.s);
    f.s[0] = 0x10;  // not mapped
    f.s[4] = 0x14;
    f.s[5] = 0x40;
    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK_EQ(out[0], 0);
}

TEST_NO_ASAN(menu_readstring_partial_heap_copy_gives_empty) {
    SYSTEM_INFO system;

    GetSystemInfo(&system);
    const size_t pageSize = system.dwPageSize;
    auto* pages = static_cast<char*>(
        rs7test::Must(VirtualAlloc(nullptr, 2 * pageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), "VirtualAlloc"));
    DWORD oldProtect;

    CHECK(VirtualProtect(pages + pageSize, pageSize, PAGE_NOACCESS, &oldProtect));
    char* text = pages + pageSize - 4;

    std::memset(text, 'a', 4);
    FakeString f;
    char out[16];

    SetHeap(f, text, 8);
    std::memset(out, 'x', sizeof out);
    rs7menu::ReadString(Addr(&f), out, sizeof out);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[8], 'x');
    VirtualFree(pages, 0, MEM_RELEASE);
}

TEST(menu_recordcount_divides_by_record_size) {
    alignas(4) uint32_t v[3] = {0x1000, 0x1000, 0};

    CHECK_EQ(rs7menu::RecordCount(Addr(v)), 0u);
    v[1] = 0x1000 + rs7menu::kRecordSize;
    CHECK_EQ(rs7menu::RecordCount(Addr(v)), 1u);
    v[1] = 0x1000 + 5 * rs7menu::kRecordSize;
    CHECK_EQ(rs7menu::RecordCount(Addr(v)), 5u);
    v[1] = 0x1000 + 5 * rs7menu::kRecordSize + 7;  // a partial record is not counted
    CHECK_EQ(rs7menu::RecordCount(Addr(v)), 5u);
}
