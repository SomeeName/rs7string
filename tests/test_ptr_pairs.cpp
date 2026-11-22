#include "d3d/hookutil.h"
#include "d3d/texlist.h"
#include "features/arrangements/menu.h"
#include "platform/protect.h"
#include "platform/safecopy.h"
#include "test.h"
#include <windows.h>

namespace {
struct Page {
    uintptr_t* slots = static_cast<uintptr_t*>(
        rsxrtest::Must(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), "VirtualAlloc"));

    ~Page() {
        Rsxr_ProtectInjectFailure(0, 0);
        VirtualFree(slots, 0, MEM_RELEASE);
    }

    void Seal() {
        DWORD old;

        CHECK(VirtualProtect(slots, 4096, PAGE_READONLY, &old));
    }

    void FailCleanup(unsigned slot) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(&slots[slot]);

        Rsxr_ProtectInjectFailure(at, at + sizeof(uintptr_t), PAGE_READONLY);
    }
};

struct Reader : RsxrMemReader {
    bool Read(uintptr_t at, void* out, size_t n) const override {
        return Rsxr_SafeCopy(out, reinterpret_cast<const void*>(at), n);
    }
};
}  // namespace

TEST(d3d_recheck_rolls_back_a_pair_when_either_slots_cleanup_fails) {
    for (unsigned failed = 0; failed < 2; ++failed) {
        Page page;
        const rsxrd3d::VtSlotPair pair{"test", 0, {0x3333, 0x4444}};
        const rsxrd3d::SlotValues original{0x1111, 0x2222};
        Reader reader;
        bool foreign = false;
        auto** vt = reinterpret_cast<void**>(page.slots);

        page.slots[0] = original.lo;
        page.slots[1] = original.hi;
        page.Seal();
        page.FailCleanup(failed);
        pair.Recheck(nullptr, vt, original, reader, &foreign);
        CHECK_EQ(page.slots[0], uintptr_t(0x1111));
        CHECK_EQ(page.slots[1], uintptr_t(0x2222));
        CHECK(!foreign);
        Rsxr_ProtectInjectFailure(0, 0);
        page.Seal();
        pair.Recheck(nullptr, vt, original, reader, &foreign);
        CHECK(pair.Ours(vt));
        CHECK(!foreign);
    }
}

TEST(menu_focus_pair_rolls_back_after_cleanup_failure_and_can_be_retried) {
    for (unsigned failed = 0; failed < 2; ++failed) {
        Page page;
        constexpr unsigned up = rsxrmenu::kFocusUp / sizeof(uintptr_t), down = rsxrmenu::kFocusDown / sizeof(uintptr_t);
        const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(page.slots));

        page.slots[up] = rsxrmenu::kStockFocusUp;
        page.slots[down] = rsxrmenu::kStockFocusDown;
        page.Seal();
        page.FailCleanup(up + failed);
        CHECK(!rsxrmenu::HookFocusSteps(address, 0x3333, 0x4444));
        CHECK_EQ(page.slots[up], uintptr_t(rsxrmenu::kStockFocusUp));
        CHECK_EQ(page.slots[down], uintptr_t(rsxrmenu::kStockFocusDown));
        Rsxr_ProtectInjectFailure(0, 0);
        page.Seal();
        CHECK(rsxrmenu::HookFocusSteps(address, 0x3333, 0x4444));
        CHECK_EQ(page.slots[up], uintptr_t(0x3333));
        CHECK_EQ(page.slots[down], uintptr_t(0x4444));
    }
}
