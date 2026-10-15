#include "core/plainpsarc.h"
#include "core/patch.h"
#include "core/pattern.h"
#include "hooks/isolate.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" __declspec(dllexport) uintptr_t g_rs7_plain_psarc = uintptr_t(Rs7PlainProvider::Unknown);
extern "C" __declspec(dllexport) uintptr_t g_rs7_skip_PP_TOC;  // hooks/stubs_psarc.cpp

namespace {
// PP_TOC: the TOC decrypt block runs from site - 3 (mov eax,[ebp-74h]) to site + 0x4D (lea ecx,[edi+34h]);
// the pattern pins every byte of it up to the add esp,28h that ends it.
constexpr uintptr_t kTocSkip = 0x4D;

struct Unit {
    const Rs7Site* site = nullptr;
    const Rs7Hook* hook = nullptr;
    uintptr_t addr = 0;
    std::vector<uint8_t> original;  // the site's `expect` (PP_SNGF: cmp + jne, 11 bytes)
    uint8_t ours[5] = {};
};

std::vector<Unit> g_unit;

void SetProvider(Rs7PlainProvider p) {
    g_rs7_plain_psarc = uintptr_t(p);
}

Rs7SiteState Live(const Unit& u) {
    return Rs7_ClassifySite(reinterpret_cast<const uint8_t*>(u.addr), u.original.data(), u.original.size(), u.ours,
                            sizeof u.ours);
}

std::string Describe(const std::vector<Rs7SiteState>& st) {
    std::string s;

    for (size_t i = 0; i < g_unit.size(); ++i) {
        char b[96];
        const uint8_t* p = reinterpret_cast<const uint8_t*>(g_unit[i].addr);

        std::snprintf(b, sizeof b, "%s%s %08X %s (%02X %02X %02X %02X %02X)", i ? ", " : "", g_unit[i].site->id,
                      unsigned(g_unit[i].addr), Rs7_SiteStateName(st[i]), p[0], p[1], p[2], p[3], p[4]);
        s += b;
    }

    return s;
}

std::vector<Rs7SiteState> States() {
    std::vector<Rs7SiteState> st;

    for (const auto& u : g_unit) {
        st.push_back(Live(u));
    }

    return st;
}

// Finds the three sites (patterns wildcard every byte an enabler or we overwrite) and binds the stubs'
// variables from the stock bytes. Empty string on success.
std::string Resolve(const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize) {
    g_unit.clear();
    const uint8_t* text = reinterpret_cast<const uint8_t*>(textVa);

    for (int i = 0; i < count; ++i) {
        const Rs7Site& s = sites[i];
        Unit u;

        u.site = &s;
        Rs7Pattern pat, exp;
        if (!Rs7_ParsePattern(s.pattern, pat) || !Rs7_ParsePattern(s.expect, exp)) {
            return std::string(s.id) + ": bad pattern";
        }

        const auto hits = Rs7_FindAll(text, textSize, pat, 2);

        if (hits.size() != 1) {
            char m[64];

            std::snprintf(m, sizeof m, "%s: %zu matches (need 1)", s.id, hits.size());
            return m;
        }

        u.addr = textVa + hits[0] + size_t(s.offset);
        u.original = exp.bytes;
        u.hook = Rs7_FindHook(s.id);
        if (!u.hook || !u.hook->stub || u.original.size() < 5) {
            return std::string(s.id) + ": no stub";
        }

        u.ours[0] = s.kind == Rs7Kind::Call ? 0xE8 : 0xE9;
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(u.hook->stub) - (u.addr + 5));

        std::memcpy(u.ours + 1, &rel, 4);
        g_unit.push_back(u);
    }

    for (const auto& u : g_unit) {  // from the stock bytes, whatever the site holds now
        if (u.hook->resume) {
            *u.hook->resume = u.addr + uintptr_t(u.site->length);
        }

        if (u.hook->target && u.site->targetRelAt >= 0) {
            int32_t rel;

            std::memcpy(&rel, u.original.data() + u.site->targetRelAt, 4);
            *u.hook->target = u.addr + uintptr_t(u.site->targetInsnEnd) + uintptr_t(rel);
        }

        if (!std::strcmp(u.site->id, "PP_TOC")) {
            g_rs7_skip_PP_TOC = u.addr + kTocSkip;
        }
    }

    return {};
}
}  // namespace

void Rs7Plain_Start(bool enabled, const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize) {
    const DWORD t0 = GetTickCount();
    std::string err;

    for (int attempt = 0; attempt < 20; ++attempt) {  // .text may still be unpacking
        err = Resolve(sites, count, textVa, textSize);
        if (err.empty()) {
            break;
        }

        Sleep(100);
    }

    if (!err.empty()) {
        Rs7Log("plain psarc: site %s - plain PSARC support off (feature %s DISABLED)", err.c_str(),
               Rs7_FeatureName(Rs7Feature::Psarc));
        Rs7_DisableFeature(Rs7Feature::Psarc);
        SetProvider(enabled ? Rs7PlainProvider::Failed : Rs7PlainProvider::Off);
        return;
    }

    std::vector<Rs7SiteState> st = States();
    const Rs7PlainDecision d = Rs7Plain_Decide(st.data(), st.size());

    if (!enabled) {
        SetProvider(Rs7PlainProvider::Off);
        Rs7Log("plain psarc: [psarc] plain=0 - rs7string leaves the sites alone; %s [%s]",
               d == Rs7PlainDecision::Enabler ? "provided by the CDLC enabler"
                                              : "nobody provides it: plain packages will not load",
               Describe(st).c_str());
        return;
    }

    if (d == Rs7PlainDecision::Enabler) {
        SetProvider(Rs7PlainProvider::Enabler);
        Rs7Log("plain psarc: provided by the CDLC enabler - all three sites left alone [%s]", Describe(st).c_str());
        return;
    }

    if (d == Rs7PlainDecision::Already) {
        SetProvider(Rs7PlainProvider::Ours);
        Rs7Log("plain psarc: already patched by rs7string [%s]", Describe(st).c_str());
        return;
    }

    // One unit in the Psarc feature group: a failed write restores the others (CAS back from ours only).
    Rs7PatchOps ops;

    ops.count = g_unit.size();
    ops.feature = [](size_t) {
        return Rs7Feature::Psarc;
    };
    ops.name = [](size_t i) {
        return g_unit[i].site->id;
    };
    ops.should = [](size_t i) {
        return !Rs7_FeatureDisabled(Rs7Feature::Psarc) && Live(g_unit[i]) == Rs7SiteState::Original;
    };
    ops.prepare = [](size_t) {
        return true;
    };
    // Review #9: the CAS writes run under the same thread freeze as the main batch (ops.range), so the
    // CDLC enabler's threads and the loaders are stopped and none stands inside a site; the compare-exchange
    // still guards against an enabler that wrote before the freeze. Inside the freeze: no logging.
    static Rs7CasResult casResult[8];

    ops.apply = [](size_t i) {
        const Unit& u = g_unit[i];
        const Rs7CasResult r = Rs7_CasCode(u.addr, u.original.data(), u.ours, sizeof u.ours);

        if (i < 8) {
            casResult[i] = r;
        }

        return r == Rs7CasResult::Written;
    };
    ops.restore = [](size_t i) {  // review #8: success = the bytes are no longer ours
        const Unit& u = g_unit[i];
        const Rs7CasResult r =
            Rs7_CasCode(u.addr, u.ours, u.original.data(), sizeof u.ours);  // only if it is still ours
        if (r == Rs7CasResult::Written) {
            return true;
        }

        return r == Rs7CasResult::Mismatch &&
               std::memcmp(reinterpret_cast<const void*>(u.addr), u.ours, sizeof u.ours) != 0;
    };
    ops.range = [](size_t i) {
        return Rs7Range{g_unit[i].addr, g_unit[i].addr + sizeof g_unit[i].ours};
    };
    ops.log = [](const std::string& m) {
        for (size_t i = 0; i < g_unit.size() && i < 8; ++i) {  // after the freeze: why a CAS failed
            const Rs7CasResult r = casResult[i];

            if (r != Rs7CasResult::Written && r != Rs7CasResult(-1)) {
                Rs7Log("plain psarc: %s compare-exchange failed (%s)", g_unit[i].site->id,
                       r == Rs7CasResult::Mismatch      ? "the bytes changed"
                       : r == Rs7CasResult::Unsupported ? "no atomic window"
                                                        : "protection");
            }

            casResult[i] = Rs7CasResult(-1);
        }

        Rs7Log("plain psarc: %s", m.c_str());
    };

    for (auto& r : casResult) {
        r = Rs7CasResult(-1);
    }

    std::vector<size_t> written;
    const Rs7PatchResult pr = Rs7_PatchIsolated(ops, written);  // never Core; a failure disables Psarc

    if (pr == Rs7PatchResult::FreezeTimeout) {
        Rs7_DisableFeature(Rs7Feature::Psarc);
    }

    if (pr == Rs7PatchResult::Unrecoverable) {  // review #8: our bytes stay; plain packages still load through them
        SetProvider(Rs7PlainProvider::Failed);
        Rs7Log("plain psarc: UNRECOVERABLE rollback - %u site(s) stay rs7string's, no retries [%s]",
               unsigned(written.size()), Describe(States()).c_str());
        return;
    }

    st = States();

    if (Rs7_FeatureDisabled(Rs7Feature::Psarc)) {
        if (Rs7Plain_Decide(st.data(), st.size()) == Rs7PlainDecision::Enabler) {
            SetProvider(Rs7PlainProvider::Enabler);
            Rs7Log("plain psarc: provided by the CDLC enabler - it patched while we did, ours restored [%s]",
                   Describe(st).c_str());
        } else {
            SetProvider(Rs7PlainProvider::Failed);
            Rs7Log("plain psarc: writing failed - plain PSARC support off [%s]", Describe(st).c_str());
        }

        return;
    }

    SetProvider(Rs7PlainProvider::Ours);
    Rs7Log("plain psarc: provided by rs7string, %u sites written %lu ms after d3d9 [%s]", unsigned(written.size()),
           GetTickCount() - t0, Describe(st).c_str());
}

void Rs7Plain_LogState(const char* when) {
    if (g_unit.empty()) {
        return;
    }

    const std::vector<Rs7SiteState> st = States();
    const Rs7PlainDecision d = Rs7Plain_Decide(st.data(), st.size());
    const Rs7PlainProvider was = Rs7PlainProvider(g_rs7_plain_psarc);

    if (was == Rs7PlainProvider::Ours && d == Rs7PlainDecision::Enabler) {
        SetProvider(Rs7PlainProvider::Enabler);
    }
    Rs7Log("plain psarc: %s: %s [%s]%s", when, Rs7Plain_ProviderName(Rs7PlainProvider(g_rs7_plain_psarc)),
           Describe(st).c_str(),
           was == Rs7PlainProvider::Ours && d == Rs7PlainDecision::Enabler
               ? " - the CDLC enabler overwrote ours (same behaviour)"
               : "");
}
