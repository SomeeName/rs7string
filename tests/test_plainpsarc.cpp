#include "core/patch.h"
#include "core/plainpsarc.h"
#include "generated/sites.generated.h"
#include "hooks/registry.h"
#include "test.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" uintptr_t g_rs7_target_PP_SNGD;
extern "C" bool __cdecl Rs7_Stub_PP_SNGD(const uint8_t*, uint32_t, const char*, uint32_t, uint8_t*, uint32_t);

namespace {
const uint8_t kSngfStock[11] = {0x83, 0x7C, 0x24, 0x68, 0x03, 0x0F, 0x85, 0x36, 0x06, 0x00, 0x00};
const uint8_t kSngfOurs[5] = {0xE9, 0x11, 0x22, 0x33, 0x44};

struct ExecPage {
    uint8_t* p;

    ExecPage() {
        p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
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
    CHECK(Rs7_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == Rs7SiteState::Original);
    std::memcpy(live, kSngfOurs, 5);  // ours: 5-byte jmp, the stock jne after it
    CHECK(Rs7_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == Rs7SiteState::Ours);
    // The plain-PSARC enabler: jmp to its own hook + 6 NOPs over all 11 bytes.
    const uint8_t enabler[11] = {0xE9, 0x01, 0x02, 0x03, 0x04, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    CHECK(Rs7_ClassifySite(enabler, kSngfStock, 11, kSngfOurs, 5) == Rs7SiteState::Foreign);
    // Our jmp but a changed tail is not ours.
    std::memcpy(live, kSngfOurs, 5);
    live[5] = 0x90;
    CHECK(Rs7_ClassifySite(live, kSngfStock, 11, kSngfOurs, 5) == Rs7SiteState::Foreign);
    // A call retargeted elsewhere (the enabler's decrypt-or-copy).
    const uint8_t callStock[5] = {0xE8, 0x37, 0x5D, 0x8E, 0x00}, callOurs[5] = {0xE8, 1, 2, 3, 0x5D};
    const uint8_t callOther[5] = {0xE8, 9, 9, 9, 0x10};

    CHECK(Rs7_ClassifySite(callStock, callStock, 5, callOurs, 5) == Rs7SiteState::Original);
    CHECK(Rs7_ClassifySite(callOurs, callStock, 5, callOurs, 5) == Rs7SiteState::Ours);
    CHECK(Rs7_ClassifySite(callOther, callStock, 5, callOurs, 5) == Rs7SiteState::Foreign);
}

TEST(plain_decide_unit) {
    using S = Rs7SiteState;
    const S stock[3] = {S::Original, S::Original, S::Original};
    const S one[3] = {S::Original, S::Foreign, S::Original};
    const S mixed[3] = {S::Ours, S::Foreign, S::Ours};
    const S ours[3] = {S::Ours, S::Ours, S::Ours};
    const S partial[3] = {S::Ours, S::Original, S::Original};

    CHECK(Rs7Plain_Decide(stock, 3) == Rs7PlainDecision::Patch);
    CHECK(Rs7Plain_Decide(one, 3) == Rs7PlainDecision::Enabler);  // never mix ours and theirs
    CHECK(Rs7Plain_Decide(mixed, 3) == Rs7PlainDecision::Enabler);
    CHECK(Rs7Plain_Decide(ours, 3) == Rs7PlainDecision::Already);
    CHECK(Rs7Plain_Decide(partial, 3) == Rs7PlainDecision::Patch);
}

TEST(plain_sng_action) {
    CHECK(Rs7Plain_SngAction(0x4A, 3, 0x100, 0x100) == Rs7SngAction::Decrypt);  // stock encrypted
    CHECK(Rs7Plain_SngAction(0x4A, 1, 0x100, 0xF0) == Rs7SngAction::Copy);
    CHECK(Rs7Plain_SngAction(0x4A, 1, 0x100, 0xEF) == Rs7SngAction::Reject);    // out too small
    CHECK(Rs7Plain_SngAction(0x4A, 1, 0x0F, 0x100) == Rs7SngAction::Reject);    // no IV slot
    CHECK(Rs7Plain_SngAction(0x4A, 1, 0x10, 0) == Rs7SngAction::Copy);          // empty payload
    CHECK(Rs7Plain_SngAction(0x4B, 1, 0x100, 0x100) == Rs7SngAction::Decrypt);  // not an SNG: the game decides
    CHECK(Rs7Plain_SngAction(0x4A, 0, 0x100, 0x100) == Rs7SngAction::Decrypt);
}

TEST(plain_sngd_stub_copies_or_decrypts) {
    const uintptr_t was = g_rs7_target_PP_SNGD;

    g_rs7_target_PP_SNGD = reinterpret_cast<uintptr_t>(&FakeDecrypt);
    g_decryptCalls = 0;
    uint8_t sng[8 + 16 + 4] = {0x4A, 0, 0, 0, 1, 0, 0, 0};  // magic, flags 1, IV slot, payload
    const uint8_t payload[4] = {0x78, 0x9C, 0x01, 0x02};

    std::memcpy(sng + 24, payload, 4);
    uint8_t out[8] = {};

    CHECK(Rs7_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, sizeof out));
    CHECK(std::memcmp(out, payload, 4) == 0);
    CHECK_EQ(g_decryptCalls, 0);
    CHECK(!Rs7_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, 3));  // payload does not fit
    sng[4] = 3;                                             // encrypted: the game's decrypt
    CHECK(Rs7_Stub_PP_SNGD(sng + 8, 20, "k", 1, out, sizeof out));
    CHECK_EQ(g_decryptCalls, 1);
    CHECK_EQ(out[0], uint8_t(0xDE));
    g_rs7_target_PP_SNGD = was;
}

TEST(plain_cas_window) {
    uintptr_t w = 0;

    CHECK(Rs7_CasWindow(0x8A3643, 5, &w) && w == 0x8A3640);  // PP_TOC: the aligned qword
    CHECK(Rs7_CasWindow(0x558C1A, 5, &w) && w == 0x558C18);  // PP_SNGF
    CHECK(Rs7_CasWindow(0x558CD4, 5, &w) && w == 0x558CD1);  // PP_SNGD: crosses the qword, not the line
    CHECK(w >> 6 == (w + 7) >> 6 && w <= 0x558CD4 && w + 8 >= 0x558CD9);
    CHECK(!Rs7_CasWindow(0x1003E, 5, &w));                 // straddles a cache line
    CHECK(Rs7_CasWindow(0x1003B, 5, &w) && w == 0x10038);  // ends exactly at the line
    CHECK(!Rs7_CasWindow(0x1000, 0, &w) && !Rs7_CasWindow(0x1000, 9, &w));
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
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 20), stock, ours, 5) == Rs7CasResult::Written);
    CHECK_EQ(pg.Call(20), 2);
    CHECK_EQ(pg.p[25], uint8_t(0xC3));  // the byte after the site is untouched
    // Already ours: a second write against the stock bytes is refused, nothing changes.
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 20), stock, other, 5) == Rs7CasResult::Mismatch);
    CHECK_EQ(pg.Call(20), 2);
    // Restore (ours -> stock), then a foreign patch: our restore no longer applies.
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 20), ours, stock, 5) == Rs7CasResult::Written);
    CHECK_EQ(pg.Call(20), 1);
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 20), stock, other, 5) == Rs7CasResult::Written);  // "the enabler"
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 20), ours, stock, 5) == Rs7CasResult::Mismatch);
    CHECK_EQ(pg.Call(20), 3);
    CHECK(Rs7_CasCode(uintptr_t(pg.p + 62), stock, ours, 5) == Rs7CasResult::Unsupported);  // straddles a line
}

TEST(plain_sites_are_one_unit_with_stubs) {
    CHECK_EQ(kRs7PsarcSiteCount, 3);

    for (int i = 0; i < kRs7PsarcSiteCount; ++i) {
        const Rs7Hook* h = Rs7_FindHook(kRs7PsarcSites[i].id);

        CHECK(h && h->stub);
        CHECK(h && Rs7_FeatureOf(*h) == Rs7Feature::Psarc);

        for (int k = 0; k < kRs7SiteCount; ++k) {
            CHECK(std::strcmp(kRs7Sites[k].id, kRs7PsarcSites[i].id) != 0);
        }
    }

    CHECK(std::strcmp(Rs7_FeatureName(Rs7Feature::Psarc), "[psarc] plain") == 0);
}

namespace {
// A fake .text with the three plain-PSARC patterns (wildcards 0x00, the site bytes = expect) at
// 0x100 / 0x400 / 0x800 of an executable page (each site inside one cache line, like the game's).
struct FakeText {
    ExecPage pg;
    uintptr_t site[3] = {};

    FakeText() {
        std::memset(pg.p, 0xCC, 4096);
        const size_t base[3] = {0x100, 0x400, 0x800};

        for (int i = 0; i < kRs7PsarcSiteCount; ++i) {
            const Rs7Site& s = kRs7PsarcSites[i];
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
    Rs7_ResetFeatureFailures();
    FakeText t;

    Rs7Plain_Start(true, kRs7PsarcSites, kRs7PsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rs7_plain_psarc, uintptr_t(Rs7PlainProvider::Ours));
    CHECK_EQ(t.At(0), uint8_t(0xE8));  // PP_SNGD: call into our stub
    CHECK_EQ(t.At(1), uint8_t(0xE9));
    CHECK_EQ(t.At(2), uint8_t(0xE9));
    const Rs7Hook* h = Rs7_FindHook("PP_SNGD");
    int32_t rel;

    std::memcpy(&rel, reinterpret_cast<const void*>(t.site[0] + 1), 4);
    CHECK_EQ(t.site[0] + 5 + uintptr_t(rel), reinterpret_cast<uintptr_t>(h->stub));
    CHECK_EQ(*reinterpret_cast<const uint8_t*>(t.site[1] + 5), uint8_t(0x0F));  // PP_SNGF keeps the stock jne
    CHECK_EQ(*Rs7_FindHook("PP_SNGF")->resume, t.site[1] + 5);
    CHECK_EQ(*Rs7_FindHook("PP_TOC")->resume, t.site[2] + 5);
    // Started again (a second instance): already ours, nothing rewritten.
    Rs7Plain_Start(true, kRs7PsarcSites, kRs7PsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rs7_plain_psarc, uintptr_t(Rs7PlainProvider::Ours));
    CHECK(!Rs7_FeatureDisabled(Rs7Feature::Psarc));
}

TEST(plain_start_leaves_an_enabler_patched_unit_alone) {
    Rs7_ResetFeatureFailures();
    FakeText t;
    // The enabler retargeted only the SNG decrypt call (one foreign site is enough).
    const uint8_t foreign[5] = {0xE8, 0x10, 0x20, 0x30, 0x40};
    uint8_t stock[5];

    std::memcpy(stock, reinterpret_cast<const void*>(t.site[0]), 5);
    CHECK(Rs7_CasCode(t.site[0], stock, foreign, 5) == Rs7CasResult::Written);
    uint8_t before[2][11];

    std::memcpy(before[0], reinterpret_cast<const void*>(t.site[1]), 11);
    std::memcpy(before[1], reinterpret_cast<const void*>(t.site[2]), 5);
    Rs7Plain_Start(true, kRs7PsarcSites, kRs7PsarcSiteCount, uintptr_t(t.pg.p), 4096);
    CHECK_EQ(g_rs7_plain_psarc, uintptr_t(Rs7PlainProvider::Enabler));
    CHECK(std::memcmp(before[0], reinterpret_cast<const void*>(t.site[1]), 11) == 0);
    CHECK(std::memcmp(before[1], reinterpret_cast<const void*>(t.site[2]), 5) == 0);
    CHECK(std::memcmp(foreign, reinterpret_cast<const void*>(t.site[0]), 5) == 0);
    CHECK(!Rs7_FeatureDisabled(Rs7Feature::Psarc));
}

TEST(plain_start_off_and_missing_sites) {
    Rs7_ResetFeatureFailures();

    {
        FakeText t;

        Rs7Plain_Start(false, kRs7PsarcSites, kRs7PsarcSiteCount, uintptr_t(t.pg.p), 4096);
        CHECK_EQ(g_rs7_plain_psarc, uintptr_t(Rs7PlainProvider::Off));
        CHECK_EQ(t.At(0), uint8_t(0xE8));
        CHECK_EQ(t.At(1), uint8_t(0x83));  // stock cmp
        CHECK_EQ(t.At(2), uint8_t(0xB9));  // stock mov ecx,10h
    }

    ExecPage empty;  // no patterns: only plain support goes

    Rs7Plain_Start(true, kRs7PsarcSites, kRs7PsarcSiteCount, uintptr_t(empty.p), 256);
    CHECK_EQ(g_rs7_plain_psarc, uintptr_t(Rs7PlainProvider::Failed));
    CHECK(Rs7_FeatureDisabled(Rs7Feature::Psarc));
    CHECK(!Rs7_FeatureDisabled(Rs7Feature::Colours));
    Rs7_ResetFeatureFailures();
}
