#include "platform/resolve.h"
#include "platform/pattern.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
std::string Err(const RsxrSite& s, const char* what) {
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

RsxrResolveResult Rsxr_ResolveAll(const RsxrSite* sites, int count, const uint8_t* text, size_t size,
                                  uintptr_t textVa) {
    RsxrResolveResult r{true, {}, {}, {}, {}};
    size_t hist[256];  // one pass over .text: each pattern scans for its rarest fixed byte

    Rsxr_ByteHistogram(text, size, hist);

    for (int i = 0; i < count; ++i) {
        const RsxrSite& s = sites[i];
        RsxrPattern pat, exp;

        if (!Rsxr_ParsePattern(s.pattern, pat) || !Rsxr_ParsePattern(s.expect, exp)) {
            r.errors.push_back(Err(s, "bad pattern"));
            r.failed.push_back(&s);
            continue;
        }

        // `expect` doubles as the rollback bytes: a wildcard there would restore 0x00
        if (std::find(exp.wild.begin(), exp.wild.end(), true) != exp.wild.end()) {
            r.errors.push_back(Err(s, "expect has a wildcard"));
            r.failed.push_back(&s);
            continue;
        }

        const auto hits = Rsxr_FindAll(text, size, pat, 2, hist);

        if (hits.size() != 1) {
            char msg[64];

            std::snprintf(msg, sizeof msg, "%zu matches (need 1)", hits.size());
            r.errors.push_back(Err(s, msg));
            r.failed.push_back(&s);
            continue;
        }

        // signed: a negative offset before the start of .text must fail, not wrap
        const int64_t soff = static_cast<int64_t>(hits[0]) + s.offset;

        if (soff < 0 || static_cast<uint64_t>(soff) + exp.bytes.size() > size) {
            r.errors.push_back(Err(s, "site runs outside .text"));
            r.failed.push_back(&s);
            continue;
        }

        const size_t off = static_cast<size_t>(soff);

        if (std::memcmp(text + off, exp.bytes.data(), exp.bytes.size()) != 0) {
            r.errors.push_back(Err(s, "bytes at the site differ from expect (other build or already patched)"));
            r.failed.push_back(&s);
            continue;
        }

        RsxrResolved res{&s, textVa + off, 0, exp.bytes};

        if (s.targetRelAt >= 0) {
            const size_t relSize = s.targetRelSize == 1 ? 1u : 4u;  // ReadRel reads 4 bytes for anything but 1

            if (off + static_cast<size_t>(s.targetRelAt) + relSize > size) {
                r.errors.push_back(Err(s, "branch target field runs past .text"));
                r.failed.push_back(&s);
                continue;
            }

            res.target = textVa + off + s.targetInsnEnd + ReadRel(text + off + s.targetRelAt, s.targetRelSize);
        }

        if (s.kind == RsxrKind::Ref) {  // data reference: the "target" is the dword at the site
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

bool Rsxr_ResolveSettled(const RsxrResolveResult& res, const std::vector<const RsxrSite*>& previous, int count) {
    return res.ok || (!res.failed.empty() && res.failed.size() * 20 <= size_t(count) && res.failed == previous);
}
