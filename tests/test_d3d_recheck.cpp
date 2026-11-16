#include "d3d/hookutil.h"
#include "d3d/texlist.h"
#include "test.h"
#include <windows.h>
#include <cstring>

namespace {
struct Slots : Rs7MemReader {
    uintptr_t values[2] = {0x1000, 0x2000};

    bool Read(uintptr_t, void* out, size_t n) const override {
        if (n != sizeof values) {
            return false;
        }

        std::memcpy(out, values, n);

        return true;
    }
};
}  // namespace

TEST(d3d_recheck_rolls_back_when_the_second_slot_write_fails) {
    SYSTEM_INFO info{};

    GetSystemInfo(&info);
    auto* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * info.dwPageSize, MEM_RESERVE, PAGE_NOACCESS));

    CHECK(pages != nullptr);
    if (!pages) {
        return;
    }

    CHECK(VirtualAlloc(pages, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE) == pages);
    auto** vt = reinterpret_cast<void**>(pages + info.dwPageSize - sizeof(void*));
    Slots mem;
    const rs7d3d::VtSlotPair slots{"test", 0, {0x3000, 0x4000}};
    bool foreign = false;

    vt[0] = reinterpret_cast<void*>(mem.values[0]);
    slots.Recheck(nullptr, vt, {mem.values[0], mem.values[1]}, mem, &foreign);
    CHECK(vt[0] == reinterpret_cast<void*>(mem.values[0]));
    CHECK(!foreign);
    // Once the second slot becomes writable, the same check must recover.
    CHECK(VirtualAlloc(pages + info.dwPageSize, info.dwPageSize, MEM_COMMIT, PAGE_READWRITE) ==
          pages + info.dwPageSize);
    vt[1] = reinterpret_cast<void*>(mem.values[1]);
    slots.Recheck(nullptr, vt, {mem.values[0], mem.values[1]}, mem, &foreign);
    CHECK(slots.Ours(vt));
    CHECK(VirtualFree(pages, 0, MEM_RELEASE));
}
