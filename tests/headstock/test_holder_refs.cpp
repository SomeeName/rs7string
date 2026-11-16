#include "features/headstock/assetload_head.h"
#include "test.h"

namespace {
struct Object {
    void** vtable;
    long refs;
    unsigned destroyed;
};

void __fastcall Destroy(Object* obj, void*) {
    ++obj->destroyed;
}
}  // namespace

TEST(headstock_releases_every_owned_holder_once_and_destroys_last_references) {
    void* vtable[] = {nullptr, reinterpret_cast<void*>(&Destroy)};
    Object shared{vtable, 127, 0}, last{vtable, 1, 0};
    rs7asset::Head head{};

    for (auto& holder : head.stockHold) {
        for (uintptr_t& p : holder) {
            p = reinterpret_cast<uintptr_t>(&shared);
        }
    }

    for (auto& holder : head.altHold) {
        for (uintptr_t& p : holder) {
            p = reinterpret_cast<uintptr_t>(&shared);
        }
    }

    for (auto& holder : head.paramsX) {
        for (uintptr_t& p : holder) {
            p = reinterpret_cast<uintptr_t>(&shared);
        }
    }

    // 126 owned slots, one of them the last reference to another object.
    head.paramsX[1][8] = reinterpret_cast<uintptr_t>(&last);
    shared.refs = 126;
    rs7asset::ReleaseParams(head);
    CHECK_EQ(shared.refs, 1L);
    CHECK_EQ(shared.destroyed, 0u);
    CHECK_EQ(last.refs, 0L);
    CHECK_EQ(last.destroyed, 1u);
    rs7asset::ReleaseParams(head);
    CHECK_EQ(shared.refs, 1L);
    CHECK_EQ(last.destroyed, 1u);
}
