#include "platform/resolve.h"
#include "platform/pattern.h"
#include <cstdio>
#include <cstring>

namespace {
std::string Err(const Rs7Site& s, const char* what) {
    return std::string(s.id) + ": " + what;
}

int32_t ReadRel(const uint8_t* p, int size) {
    if (size == 1) {
        return static_cast<int8_t>(p[0]);
    }

    int32_t v;

    std::memcpy(&v, p, 4);

    return v;
}
}  // namespace

Rs7ResolveResult Rs7_ResolveAll(const Rs7Site* sites, int count, const uint8_t* text, size_t size, uintptr_t textVa) {
    Rs7ResolveResult r{true, {}, {}, {}, {}};
    size_t hist[256];  // one pass over .text: each pattern scans for its rarest fixed byte

    Rs7_ByteHistogram(text, size, hist);

    for (int i = 0; i < count; ++i) {
        const Rs7Site& s = sites[i];
        Rs7Pattern pat, exp;

        if (!Rs7_ParsePattern(s.pattern, pat) || !Rs7_ParsePattern(s.expect, exp)) {
            r.errors.push_back(Err(s, "bad pattern"));
            r.failed.push_back(&s);
            continue;
        }

        const auto hits = Rs7_FindAll(text, size, pat, 2, hist);

        if (hits.size() != 1) {
            char msg[64];

            std::snprintf(msg, sizeof msg, "%zu matches (need 1)", hits.size());
            r.errors.push_back(Err(s, msg));
            r.failed.push_back(&s);
            continue;
        }

        const size_t off = hits[0] + static_cast<size_t>(s.offset);

        if (off + exp.bytes.size() > size || std::memcmp(text + off, exp.bytes.data(), exp.bytes.size()) != 0) {
            r.errors.push_back(Err(s, "bytes at the site differ from expect (other build or already patched)"));
            r.failed.push_back(&s);
            continue;
        }

        Rs7Resolved res{&s, textVa + off, 0, exp.bytes};

        if (s.targetRelAt >= 0) {
            res.target = textVa + off + s.targetInsnEnd + ReadRel(text + off + s.targetRelAt, s.targetRelSize);
        }

        if (s.kind == Rs7Kind::Ref) {  // data reference: the "target" is the dword at the site
            if (off + 4 > size) {
                r.errors.push_back(Err(s, "ref site runs past .text"));
                r.failed.push_back(&s);
                continue;
            }

            uint32_t v;

            std::memcpy(&v, text + off, 4);
            res.target = v;
        }

        r.sites.push_back(res);
    }

    r.ok = r.errors.empty();
    r.resolved = r.sites;
    if (!r.ok) {
        r.sites.clear();
    }

    return r;
}
