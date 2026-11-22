#pragma once
#include "features/arrangements/menu.h"

namespace rsxrarr {
// One window per co-op list page; each slot owns the builder's spare focus lists across page changes.
struct PageState {
    uint32_t page = 0;
    uint32_t offset = 0;  // records above the first shown entry
    uint32_t spareFocus[rsxrmenu::kFocusListDwords] = {};

    void Reset(uint32_t nextPage) {
        // Keep both spare vectors: the next page's builder clears and refills them with the game's allocator.
        page = nextPage;
        offset = 0;
    }
};

struct PageStates {
    PageState pages[2];

    PageState* Find(uint32_t page) {
        for (PageState& p : pages) {
            if (page && p.page == page) {
                return &p;
            }
        }

        return nullptr;
    }

    PageState* Claim(uint32_t page) {
        if (!page) {
            return nullptr;
        }
        if (PageState* p = Find(page)) {
            return p;
        }

        for (PageState& p : pages) {
            if (!p.page) {
                p.Reset(page);
                return &p;
            }
        }

        return nullptr;  // both players are live: never evict either one's window
    }

    void Retire(uint32_t page) {
        if (PageState* p = Find(page)) {
            p->Reset(0);
        }
    }
};
}  // namespace rsxrarr
