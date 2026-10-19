// The plain-PSARC unit's game glue (psarc.h): the patching that writes its three sites (PP_TOC and PP_SNGF are
// mid-hooks, hooks_mid.cpp; PP_SNGD is a C function, below). Same behaviour as
// the plain-PSARC enabler build (RSCDLCEnabler-TooManyCoresFix PR #1, PlainPsarc.cpp); the stubs are exported for
// tools/tests/test_emu_psarc.py.
#include "features/psarc/psarc.h"
#include "model/groups.h"
#include "platform/isolate.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/patch.h"
#include "platform/pattern.h"
#include "platform/resolve.h"
#include "platform/retry.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

extern "C" __declspec(dllexport) uintptr_t g_rs7_plain_psarc = uintptr_t(Rs7PlainProvider::Unknown);

//   PP_TOC   0x8A3643 PSARC TOC loader: plain TOC (archive flag bit 4 clear) skips the decrypt block
//   PP_SNGF  0x558C1A SNG loader: flags 1 (plain) passes the flags check
//   PP_SNGD  0x558CD4 SNG loader: the call of the SNG decrypt becomes decrypt-or-copy (a C function)
extern "C" __declspec(dllexport) uintptr_t g_rs7_target_PP_SNGD = 0;  // the game's SNG decrypt (0xE3EA10)
extern "C" uintptr_t g_rs7_skip_PP_TOC;                               // hooks_mid.cpp

namespace {
// PP_TOC: the TOC decrypt block runs from site - 3 (mov eax,[ebp-74h]) to site + 0x4D (lea ecx,[edi+34h]);
// the pattern pins every byte of it up to the add esp,28h that ends it.
constexpr uintptr_t kTocSkip = 0x4D;
constexpr Rs7Group kPsarc = Rs7_Group(Rs7Feature::Psarc);

struct Unit {
    const Rs7Site* site = nullptr;
    const Rs7Hook* hook = nullptr;
    uintptr_t addr = 0;
    std::vector<uint8_t> original;  // the site's `expect` (PP_SNGF: cmp + jne, 11 bytes)
    uint8_t ours[5] = {};
};

std::vector<Unit> g_unit;

// The mid-hook entries of the unit's mid-hook sites (PP_TOC, PP_SNGF), one per site index, built once per address
// outside the freeze from a fixed pool. A unit resolved at another address (only the native tests' fake .text) takes a
// new entry from the pool; the old one stays, since a thread could still run in it.
struct MidEntry {
    uintptr_t at = 0;
    Rs7Mid::Hook* hook = nullptr;
};

MidEntry g_mid[4];
Rs7Mid::Hook g_midPool[32];
size_t g_midUsed = 0;
size_t g_midLimit = 0;  // tests: 0 = the whole pool

// The address our jmp goes to: the row's stub, or its mid-hook entry (0: the entry could not be built).
uintptr_t OurTarget(size_t index, const Rs7Site& s, const Rs7Hook& h, uintptr_t addr, const std::vector<uint8_t>& o) {
    if (!h.IsMid()) {
        return reinterpret_cast<uintptr_t>(h.stub);
    }

    MidEntry& m = g_mid[index];

    if (m.at == addr && m.hook) {
        return m.hook->Entry();
    }

    m = MidEntry{};
    if (g_midUsed >= (g_midLimit ? g_midLimit : std::size(g_midPool)) || g_midUsed >= std::size(g_midPool)) {
        return 0;
    }

    Rs7Mid::Hook& hook = g_midPool[g_midUsed++];

    if (!Rs7_PrepareMid(hook, Rs7Resolved{&s, addr, 0, o}, h)) {
        return 0;
    }

    m = MidEntry{addr, &hook};

    return hook.Entry();
}

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
std::string Resolve(const Rs7PlainHost& host, const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize) {
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
        u.hook = host.findHook(s.id);
        if (!u.hook || !(u.hook->stub || u.hook->IsMid()) || u.original.size() < 5 || size_t(i) >= std::size(g_mid)) {
            return std::string(s.id) + ": no stub";
        }

        const uintptr_t ours = OurTarget(size_t(i), s, *u.hook, u.addr, u.original);

        if (!ours) {
            return std::string(s.id) + ": mid-hook entry rejected";
        }

        u.ours[0] = s.kind == Rs7Kind::Call ? 0xE8 : 0xE9;
        const int32_t rel = int32_t(ours - (u.addr + 5));

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

// Resolves the sites, retrying while .text may still be unpacking. On failure disables Psarc and returns false.
bool ResolveWithRetry(const Rs7PlainHost& host, bool enabled, const Rs7Site* sites, int count, uintptr_t textVa,
                      size_t textSize) {
    std::string err;

    Rs7_Retry(20, 100, [&] {  // .text may still be unpacking
        err = Resolve(host, sites, count, textVa, textSize);

        return err.empty();
    });

    if (!err.empty()) {
        Rs7Log("plain psarc: site %s - plain PSARC support off (feature %s DISABLED)", err.c_str(),
               Rs7_FeatureName(Rs7Feature::Psarc));
        host.groups->disable(kPsarc);
        SetProvider(enabled ? Rs7PlainProvider::Failed : Rs7PlainProvider::Off);
        return false;
    }

    return true;
}

// True (provider set and logged) when nothing is to be written: plain PSARC is off, or the CDLC enabler or an
// earlier start already provides it.
bool SettledWithoutWriting(bool enabled, Rs7PlainDecision d, const std::vector<Rs7SiteState>& st) {
    if (!enabled) {
        SetProvider(Rs7PlainProvider::Off);
        Rs7Log("plain psarc: [psarc] plain=0 - rs7string leaves the sites alone; %s [%s]",
               d == Rs7PlainDecision::Enabler ? "provided by the CDLC enabler"
                                              : "nobody provides it: plain packages will not load",
               Describe(st).c_str());
        return true;
    }

    if (d == Rs7PlainDecision::Enabler) {
        SetProvider(Rs7PlainProvider::Enabler);
        Rs7Log("plain psarc: provided by the CDLC enabler - all three sites left alone [%s]", Describe(st).c_str());
        return true;
    }

    if (d == Rs7PlainDecision::Already) {
        SetProvider(Rs7PlainProvider::Ours);
        Rs7Log("plain psarc: already patched by rs7string [%s]", Describe(st).c_str());
        return true;
    }

    return false;
}

// One unit in the Psarc feature group: a failed write restores the others (CAS back from ours only).
Rs7PatchOps CasPatchOps(const Rs7GroupPolicy* groups) {
    Rs7PatchOps ops;

    ops.count = g_unit.size();
    ops.group = [](size_t) {
        return kPsarc;
    };
    ops.name = [](size_t i) {
        return g_unit[i].site->id;
    };
    ops.should = [groups](size_t i) {
        return !groups->disabled(kPsarc) && Live(g_unit[i]) == Rs7SiteState::Original;
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

    return ops;
}

// Sets the provider from the patch outcome and logs it.
void ReportPatch(const Rs7GroupPolicy& groups, Rs7PatchResult pr, const std::vector<size_t>& written, DWORD t0) {
    if (pr == Rs7PatchResult::FreezeTimeout) {
        groups.disable(kPsarc);
    }

    if (pr == Rs7PatchResult::Unrecoverable) {  // review #8: our bytes stay; plain packages still load through them
        SetProvider(Rs7PlainProvider::Failed);
        Rs7Log("plain psarc: UNRECOVERABLE rollback - %u site(s) stay rs7string's, no retries [%s]",
               unsigned(written.size()), Describe(States()).c_str());
        return;
    }

    const std::vector<Rs7SiteState> st = States();

    if (groups.disabled(kPsarc)) {
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

}  // namespace

void Rs7Plain_StartFromIni(const Rs7PlainHost& host, const wchar_t* ini, const Rs7Site* sites, int count,
                           uintptr_t textVa, size_t textSize) {
    // [debug] plain_delay_ms (test only): start later, so a CDLC enabler with its own plain-PSARC patches gets
    // there first (the coexistence path).
    if (const UINT delay = GetPrivateProfileIntW(L"debug", L"plain_delay_ms", 0, ini)) {
        Rs7Log("debug: plain_delay_ms=%u -> plain PSARC starts late (test only)", delay < 30000 ? delay : 30000u);
        Sleep(delay < 30000 ? delay : 30000u);
    }

    Rs7Plain_Start(host, GetPrivateProfileIntW(L"psarc", L"plain", 1, ini) != 0, sites, count, textVa, textSize);
}

void Rs7Plain_Start(const Rs7PlainHost& host, bool enabled, const Rs7Site* sites, int count, uintptr_t textVa,
                    size_t textSize) {
    const DWORD t0 = GetTickCount();
    if (!ResolveWithRetry(host, enabled, sites, count, textVa, textSize)) {
        return;
    }

    const std::vector<Rs7SiteState> st = States();
    if (SettledWithoutWriting(enabled, Rs7Plain_Decide(st.data(), st.size()), st)) {
        return;
    }

    std::vector<size_t> written;
    const Rs7PatchResult pr =
        Rs7_PatchIsolated(*host.groups, CasPatchOps(host.groups), written);  // never Core; a failure disables Psarc

    ReportPatch(*host.groups, pr, written, t0);
}

size_t Rs7Plain_MidPoolUsed() {
    return g_midUsed;
}

void Rs7Plain_SetMidPoolLimit(size_t limit) {
    g_midLimit = limit;

    for (MidEntry& m : g_mid) {  // the next resolve takes new entries (a fake .text may reuse an old address)
        m = MidEntry{};
    }
}

uintptr_t Rs7Plain_MidEntry(const char* id) {
    for (size_t i = 0; i < g_unit.size() && i < std::size(g_mid); ++i) {
        if (!std::strcmp(g_unit[i].site->id, id) && g_mid[i].hook && g_mid[i].at == g_unit[i].addr) {
            return g_mid[i].hook->Entry();
        }
    }

    return 0;
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

// ---- PP_SNGD (PP_TOC and PP_SNGF: hooks_mid.cpp)

// PP_SNGD - called in place of the game's SNG decrypt (cdecl, 6 args, bool in AL). src points just
// past the 8-byte SNG header and starts with the 16-byte IV slot; the game expects the payload at out[0].
using Rs7SngDecrypt = bool(__cdecl*)(const uint8_t* src, uint32_t len, const char* key, uint32_t keyLen, uint8_t* out,
                                     uint32_t outLen);

extern "C" __declspec(dllexport) bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t* src, uint32_t len, const char* key,
                                                               uint32_t keyLen, uint8_t* out, uint32_t outLen) {
    // Even encrypted input needs the IV slot. Reject it before inspecting the preceding header.
    if (!src || len < 0x10) {
        return false;
    }

    uint32_t magic, flags;

    std::memcpy(&magic, src - 8, 4);
    std::memcpy(&flags, src - 4, 4);

    switch (Rs7Plain_SngAction(magic, flags, len, outLen)) {
        case Rs7SngAction::Copy:
            std::memmove(out, src + 0x10, len - 0x10);
            return true;
        case Rs7SngAction::Reject:
            return false;
        default:
            return reinterpret_cast<Rs7SngDecrypt>(g_rs7_target_PP_SNGD)(src, len, key, keyLen, out, outLen);
    }
}
