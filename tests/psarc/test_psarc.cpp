#include "features/psarc/psarc.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "platform/patch.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" uintptr_t g_rsxr_target_PP_SNGD;
extern "C" bool __cdecl Rsxr_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);

namespace {
const uint8_t kSngfStock[11] = {0x83, 0x7C, 0x24, 0x68, 0x03, 0x0F, 0x85, 0x36, 0x06, 0x00, 0x00};
const uint8_t kSngfOurs[5] = {0xE9, 0x11, 0x22, 0x33, 0x44};

struct ExecPage {
    uint8_t* p;

    ExecPage() {
        p = static_cast<uint8_t*>(
            rsxrtest::Must(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), "VirtualAlloc"));
        std::memset(p, 0xCC, 4096);
    }

    ~ExecPage() { VirtualFree(p, 0, MEM_RELEASE); }

    void Seal() {
        DWORD old;

        VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &old);
    }

    int Call(size_t off) { return reinterpret_cast<int(__cdecl*)()>(p + off)(); }
};

void EmitReturn(uint8_t* at, uint32_t value) {  // mov eax, imm32; ret
    at[0] = 0xB8;
    std::memcpy(at + 1, &value, 4);
    at[5] = 0xC3;
}

void Jmp(uint8_t* out, const uint8_t* from, const uint8_t* to) {
    out[0] = 0xE9;
    const int32_t rel = int32_t(to - (from + 5));

    std::memcpy(out + 1, &rel, 4);
}

int g_decryptCalls = 0;

bool __cdecl FakeDecrypt(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t* out, uint32_t) {
    ++g_decryptCalls;
    out[0] = 0xDE;

    return true;
}
}  // namespace

TEST(plain_classify_stock_ours_foreign) {
    uint8_t live[11];

    std::memcpy(live, kSngfStock, 11);
    CHECK(Rsxr_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == RsxrSiteState::Original);
    std::memcpy(live, kSngfOurs, 5);  // ours: 5-byte jmp, the stock jne after it
    CHECK(Rsxr_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == RsxrSiteState::Ours);
    // The plain-PSARC enabler: jmp to its own hook + 6 NOPs over all 11 bytes.
    const uint8_t enabler[11] = {0xE9, 0x01, 0x02, 0x03, 0x04, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    CHECK(Rsxr_ClassifySite(enabler, kSngfStock, 11, kSngfOurs, 5) == RsxrSiteState::Foreign);
    // Our jmp but a changed tail is not ours.
    std::memcpy(live, kSngfOurs, 5);
    live[5] = 0x90;
    CHECK(Rsxr_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == RsxrSiteState::Foreign);
    // A call retargeted elsewhere (the enabler's decrypt-or-copy).
    const uint8_t callStock[5] = {0xE8, 0x37, 0x5D, 0x8E, 0x00}, callOurs[5] = {0xE8, 1, 2, 3, 0x5D};
    const uint8_t callOther[5] = {0xE8, 9, 9, 9, 0x10};

    CHECK(Rsxr_ClassifySite(callStock, callStock, 5, callOurs, 5) == RsxrSiteState::Original);
    CHECK(Rsxr_ClassifySite(callOurs, callStock, 5, callOurs, 5) == RsxrSiteState::Ours);
    CHECK(Rsxr_ClassifySite(callOther, callStock, 5, callOurs, 5) == RsxrSiteState::Foreign);
}

TEST(plain_decide_unit) {
    using S = RsxrSiteState;
    const S stock[3] = {S::Original, S::Original, S::Original};
    const S one[3] = {S::Original, S::Foreign, S::Original};
    const S mixed[3] = {S::Ours, S::Foreign, S::Ours};
    const S ours[3] = {S::Ours, S::Ours, S::Ours};
    const S partial[3] = {S::Ours, S::Original, S::Original};

    CHECK(RsxrPlain_Decide(stock, 3) == RsxrPlainDecision::Patch);
    CHECK(RsxrPlain_Decide(one, 3) == RsxrPlainDecision::Enabler);  // never mix ours and theirs
    CHECK(RsxrPlain_Decide(mixed, 3) == RsxrPlainDecision::Enabler);
    CHECK(RsxrPlain_Decide(ours, 3) == RsxrPlainDecision::Already);
    CHECK(RsxrPlain_Decide(partial, 3) == RsxrPlainDecision::Patch);
}

TEST(plain_sng_action) {
    CHECK(RsxrPlain_SngAction(0x4A, 3, 0x100, 0x100) == RsxrSngAction::Decrypt);  // stock encrypted
    CHECK(RsxrPlain_SngAction(0x4A, 1, 0x100, 0xF0) == RsxrSngAction::Copy);
    CHECK(RsxrPlain_SngAction(0x4A, 1, 0x100, 0xEF) == RsxrSngAction::Reject);    // out too small
    CHECK(RsxrPlain_SngAction(0x4A, 1, 0x0F, 0x100) == RsxrSngAction::Reject);    // no IV slot
    CHECK(RsxrPlain_SngAction(0x4A, 1, 0x10, 0) == RsxrSngAction::Copy);          // empty payload
    CHECK(RsxrPlain_SngAction(0x4B, 1, 0x100, 0x100) == RsxrSngAction::Decrypt);  // not an SNG: the game decides
    CHECK(RsxrPlain_SngAction(0x4A, 0, 0x100, 0x100) == RsxrSngAction::Decrypt);
}

TEST(plain_sngd_stub_copies_or_decrypts) {
    rsxrtest::Restore restore_g_rsxr_target_PP_SNGD(g_rsxr_target_PP_SNGD);

    g_rsxr_target_PP_SNGD = reinterpret_cast<uintptr_t>(&FakeDecrypt);
    g_decryptCalls = 0;
    uint8_t sng[8 + 16 + 4] = {0x4A, 0, 0, 0, 1, 0, 0, 0};  // magic, flags 1, IV slot, payload
    const uint8_t payload[4] = {0x78, 0x9C, 0x01, 0x02};

    std::memcpy(sng + 24, payload, 4);
    uint8_t out[8] = {};

    CHECK(Rsxr_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, sizeof out));
    CHECK(std::memcmp(out, payload, 4) == 0);
    CHECK_EQ(g_decryptCalls, 0);
    CHECK(!Rsxr_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, 3));  // payload does not fit
    sng[4] = 3;                                              // encrypted: the game's decrypt
    CHECK(Rsxr_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, sizeof out));
    CHECK_EQ(g_decryptCalls, 1);
    CHECK_EQ(out[0], uint8_t(0xDE));
}

TEST(plain_cas_window) {
    uintptr_t w = 0;

    CHECK(Rsxr_CasWindow(0x8A3643, 5, &w) && w == 0x8A3640);  // PP_TOC: the aligned qword
    CHECK(Rsxr_CasWindow(0x558C1A, 5, &w) && w == 0x558C18);  // PP_SNGF
    CHECK(Rsxr_CasWindow(0x558CD4, 5, &w) && w == 0x558CD1);  // PP_SNGD: crosses the qword, not the line
    CHECK(w >> 6 == (w + 7) >> 6 && w <= 0x558CD4 && w + 8 >= 0x558CD9);
    CHECK(!Rsxr_CasWindow(0x1003E, 5, &w));                 // straddles a cache line
    CHECK(Rsxr_CasWindow(0x1003B, 5, &w) && w == 0x10038);  // ends exactly at the line
    CHECK(!Rsxr_CasWindow(0x1000, 0, &w) && !Rsxr_CasWindow(0x1000, 9, &w));
}

TEST(plain_cas_code_writes_only_over_expect) {
    ExecPage pg;

    EmitReturn(pg.p + 20, 1);  // 20 % 8 = 4: a 5-byte jmp there crosses the qword (like PP_SNGD)
    EmitReturn(pg.p + 128, 2);
    EmitReturn(pg.p + 192, 3);
    uint8_t stock[5], ours[5], other[5];

    std::memcpy(stock, pg.p + 20, 5);
    Jmp(ours, pg.p + 20, pg.p + 128);
    Jmp(other, pg.p + 20, pg.p + 192);
    pg.Seal();
    CHECK_EQ(pg.Call(20), 1);
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 20), stock, ours, 5) == RsxrCasResult::Written);
    CHECK_EQ(pg.Call(20), 2);
    CHECK_EQ(pg.p[25], uint8_t(0xC3));  // the byte after the site is untouched
    // Already ours: a second write against the stock bytes is refused, nothing changes.
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 20), stock, other, 5) == RsxrCasResult::Mismatch);
    CHECK_EQ(pg.Call(20), 2);
    // Restore (ours -> stock), then a foreign patch: our restore no longer applies.
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 20), ours, stock, 5) == RsxrCasResult::Written);
    CHECK_EQ(pg.Call(20), 1);
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 20), stock, other, 5) == RsxrCasResult::Written);  // "the enabler"
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 20), ours, stock, 5) == RsxrCasResult::Mismatch);
    CHECK_EQ(pg.Call(20), 3);
    CHECK(Rsxr_CasCode(uintptr_t(pg.p + 62), stock, ours, 5) == RsxrCasResult::Unsupported);  // straddles a line
}

TEST(plain_sites_are_one_unit_with_hooks) {
    CHECK_EQ(kRsxrPsarcSiteCount, 3);

    for (int i = 0; i < kRsxrPsarcSiteCount; ++i) {
        const RsxrHook* h = Rsxr_FindHook(kRsxrPsarcSites[i].id);

        CHECK(h && (h->stub || h->IsMid()));  // PP_SNGD a C function, PP_TOC / PP_SNGF mid-hooks
        CHECK(h && Rsxr_FeatureOf(*h) == RsxrFeature::Psarc);

        for (int k = 0; k < kRsxrSiteCount; ++k) {
            CHECK(std::strcmp(kRsxrSites[k].id, kRsxrPsarcSites[i].id) != 0);
        }
    }

    CHECK(std::strcmp(Rsxr_FeatureName(RsxrFeature::Psarc), "[psarc] plain") == 0);
}

namespace {
// The destination of the E9 rel32 at `site`.
uintptr_t JmpTarget(uintptr_t site) {
    int32_t rel;

    std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), 4);

    return *reinterpret_cast<const uint8_t*>(site) == 0xE9 ? site + 5 + uintptr_t(rel) : 0;
}

// A fake .text with the three plain-PSARC patterns (wildcards 0x00, the site bytes = expect) at
// 0x100 / 0x400 / 0x800 of an executable page (each site inside one cache line, like the game's).
struct FakeText {
    ExecPage pg;
    uintptr_t site[3] = {};

    FakeText() {
        std::memset(pg.p, 0xCC, 4096);
        const size_t base[3] = {0x100, 0x400, 0x800};

        for (int i = 0; i < kRsxrPsarcSiteCount; ++i) {
            const RsxrSite& s = kRsxrPsarcSites[i];
            uint8_t* at = pg.p + base[i];
            size_t n = 0;

            for (const char* c = s.pattern; *c;) {
                if (*c == ' ') {
                    ++c;
                    continue;
                }

                at[n++] = c[0] == '?' ? 0x00 : uint8_t(std::strtoul(std::string(c, 2).c_str(), nullptr, 16));
                c += 2;
            }

            site[i] = uintptr_t(at + s.offset);
            size_t k = 0;

            for (const char* c = s.expect; *c;) {
                if (*c == ' ') {
                    ++c;
                    continue;
                }

                reinterpret_cast<uint8_t*>(site[i])[k++] =
                    uint8_t(std::strtoul(std::string(c, 2).c_str(), nullptr, 16));
                c += 2;
            }
        }

        pg.Seal();
    }

    uint8_t At(int i) const { return *reinterpret_cast<const uint8_t*>(site[i]); }
};
}  // namespace

TEST(plain_start_patches_a_stock_unit) {
    Rsxr_ResetFeatureFailures();
    FakeText t;

    RsxrPlain_Start(kRsxrPlainHost, true, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Ours));
    CHECK_EQ(t.At(0), uint8_t(0xE8));  // PP_SNGD: call into our stub
    CHECK_EQ(t.At(1), uint8_t(0xE9));
    CHECK_EQ(t.At(2), uint8_t(0xE9));
    const RsxrHook* h = Rsxr_FindHook("PP_SNGD");
    int32_t rel;

    std::memcpy(&rel, reinterpret_cast<const void*>(t.site[0] + 1), 4);
    CHECK_EQ(t.site[0] + 5 + uintptr_t(rel), reinterpret_cast<uintptr_t>(h->stub));
    CHECK_EQ(*reinterpret_cast<const uint8_t*>(t.site[1] + 5), uint8_t(0x0F));  // PP_SNGF keeps the stock jne
    CHECK_EQ(*Rsxr_FindHook("PP_SNGF")->resume, t.site[1] + 5);
    CHECK(Rsxr_FindHook("PP_TOC")->resume == nullptr);  // the plain path continues at g_rsxr_skip_PP_TOC
    // PP_SNGF / PP_TOC: the jmp goes to the site's mid-hook entry, which replays the stock bytes and calls the
    // callback.
    const uintptr_t entries[2] = {JmpTarget(t.site[1]), JmpTarget(t.site[2])};

    CHECK(entries[0] && entries[1] && entries[0] != entries[1]);
    CHECK_EQ(entries[0], RsxrPlain_MidEntry("PP_SNGF"));
    CHECK_EQ(entries[1], RsxrPlain_MidEntry("PP_TOC"));
    // Started again (a second instance): already ours, nothing rewritten, no new entry built.
    const size_t used = RsxrPlain_MidPoolUsed();

    RsxrPlain_Start(kRsxrPlainHost, true, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Ours));
    CHECK_EQ(RsxrPlain_MidPoolUsed(), used);
    CHECK_EQ(JmpTarget(t.site[1]), entries[0]);
    CHECK_EQ(JmpTarget(t.site[2]), entries[1]);
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Psarc));
}

// The entry pool exhausted: a clean resolve error (the unit stays stock, only plain support goes), no write.
TEST(plain_start_with_an_exhausted_entry_pool_fails_cleanly) {
    Rsxr_ResetFeatureFailures();
    FakeText t;

    RsxrPlain_SetMidPoolLimit(RsxrPlain_MidPoolUsed());
    RsxrPlain_Start(kRsxrPlainHost, true, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(t.pg.p), 4096);
    RsxrPlain_SetMidPoolLimit(0);
    CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Failed));
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Psarc));
    CHECK_EQ(t.At(0), uint8_t(0xE8));
    CHECK_EQ(t.At(1), uint8_t(0x83));  // stock cmp
    CHECK_EQ(t.At(2), uint8_t(0xB9));  // stock mov ecx,10h
    Rsxr_ResetFeatureFailures();
}

// The enabler wrote first: with the mid-hook rows (PP_TOC, PP_SNGF) the unit is left alone, every byte as it was.
TEST(plain_start_leaves_an_enabler_patched_unit_alone) {
    Rsxr_ResetFeatureFailures();
    FakeText t;
    // The enabler retargeted only the SNG decrypt call (one foreign site is enough).
    const uint8_t foreign[5] = {0xE8, 0x10, 0x20, 0x30, 0x40};
    uint8_t stock[5];

    std::memcpy(stock, reinterpret_cast<const void*>(t.site[0]), 5);
    CHECK(Rsxr_CasCode(t.site[0], stock, foreign, 5) == RsxrCasResult::Written);
    uint8_t before[2][11];

    std::memcpy(before[0], reinterpret_cast<const void*>(t.site[1]), 11);
    std::memcpy(before[1], reinterpret_cast<const void*>(t.site[2]), 5);
    RsxrPlain_Start(kRsxrPlainHost, true, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Enabler));
    CHECK(std::memcmp(before[0], reinterpret_cast<const void*>(t.site[1]), 11) == 0);
    CHECK(std::memcmp(before[1], reinterpret_cast<const void*>(t.site[2]), 5) == 0);
    CHECK(std::memcmp(foreign, reinterpret_cast<const void*>(t.site[0]), 5) == 0);
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Psarc));
}

TEST(plain_start_off_and_missing_sites) {
    Rsxr_ResetFeatureFailures();

    {
        FakeText t;

        RsxrPlain_Start(kRsxrPlainHost, false, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(t.pg.p), 4096);
        CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Off));
        CHECK_EQ(t.At(0), uint8_t(0xE8));
        CHECK_EQ(t.At(1), uint8_t(0x83));  // stock cmp
        CHECK_EQ(t.At(2), uint8_t(0xB9));  // stock mov ecx,10h
    }

    ExecPage empty;  // no patterns: only plain support goes

    RsxrPlain_Start(kRsxrPlainHost, true, kRsxrPsarcSites, kRsxrPsarcSiteCount, uintptr_t(empty.p), 256);
    CHECK_EQ(g_rsxr_plain_psarc, uintptr_t(RsxrPlainProvider::Failed));
    CHECK(Rsxr_FeatureDisabled(RsxrFeature::Psarc));
    CHECK(!Rsxr_FeatureDisabled(RsxrFeature::Colours));
    Rsxr_ResetFeatureFailures();
}
