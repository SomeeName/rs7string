#include "platform/hooktable.h"
#include "platform/resolve.h"
#include "platform/sites.h"
#include <cstring>

const RsxrHook* Rsxr_FindHookIn(const RsxrHookTable* const* tables, size_t n, const char* id) {
    for (size_t k = 0; k < n; ++k) {
        const RsxrHookTable* t = tables[k];

        for (size_t i = 0; i < t->count; ++i) {
            if (std::strcmp(t->hooks[i].id, id) == 0) {
                return &t->hooks[i];
            }
        }
    }

    return nullptr;
}

const RsxrHookTable* Rsxr_TableOf(const RsxrHookTable* const* tables, size_t n, const RsxrHook& h) {
    for (size_t k = 0; k < n; ++k) {
        if (tables[k]->Has(h)) {
            return tables[k];
        }
    }

    return nullptr;
}

bool Rsxr_PrepareMid(RsxrMid::Hook& mid, const RsxrResolved& r, const RsxrHook& h) {
    if (h.midCall == RsxrMid::Call::Wrap) {
        return (r.site->kind == RsxrKind::Call || r.site->kind == RsxrKind::Jmp) &&
               mid.PrepareWrap(r.addr, r.original.data(), r.original.size(), r.site->kind == RsxrKind::Call, h.midWrap,
                               h.midBefore, h.callback, h.midMode);
    }

    if (r.site->kind == RsxrKind::Call) {
        return mid.PrepareCall(r.addr, r.original.data(), r.original.size(), h.callback, h.midMode, h.midCall);
    }

    return (r.site->kind == RsxrKind::Jmp || r.site->kind == RsxrKind::JmpAtomic8) &&
           mid.Prepare(r.addr, r.original.data(), r.original.size(), h.callback, h.midMode);
}
