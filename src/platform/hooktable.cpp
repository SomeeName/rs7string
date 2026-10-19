#include "platform/hooktable.h"
#include "platform/resolve.h"
#include "platform/sites.h"
#include <cstring>

const Rs7Hook* Rs7_FindHookIn(const Rs7HookTable* const* tables, size_t n, const char* id) {
    for (size_t k = 0; k < n; ++k) {
        const Rs7HookTable* t = tables[k];

        for (size_t i = 0; i < t->count; ++i) {
            if (std::strcmp(t->hooks[i].id, id) == 0) {
                return &t->hooks[i];
            }
        }
    }

    return nullptr;
}

const Rs7HookTable* Rs7_TableOf(const Rs7HookTable* const* tables, size_t n, const Rs7Hook& h) {
    for (size_t k = 0; k < n; ++k) {
        if (tables[k]->Has(h)) {
            return tables[k];
        }
    }

    return nullptr;
}

bool Rs7_PrepareMid(Rs7Mid::Hook& mid, const Rs7Resolved& r, const Rs7Hook& h) {
    if (h.midCall == Rs7Mid::Call::Wrap) {
        return (r.site->kind == Rs7Kind::Call || r.site->kind == Rs7Kind::Jmp) &&
               mid.PrepareWrap(r.addr, r.original.data(), r.original.size(), r.site->kind == Rs7Kind::Call, h.midWrap,
                               h.midBefore, h.callback, h.midMode);
    }

    if (r.site->kind == Rs7Kind::Call) {
        return mid.PrepareCall(r.addr, r.original.data(), r.original.size(), h.callback, h.midMode, h.midCall);
    }

    return (r.site->kind == Rs7Kind::Jmp || r.site->kind == Rs7Kind::JmpAtomic8) &&
           mid.Prepare(r.addr, r.original.data(), r.original.size(), h.callback, h.midMode);
}
