#include "platform/resolve.h"
#include "test.h"
#include <cstring>
#include <vector>

namespace {
std::vector<uint8_t> Text() {
    std::vector<uint8_t> t(64, 0x00);
    const uint8_t s2[] = {0x8B, 0x75, 0x0C, 0x74, 0x36, 0x83, 0xFA, 0x05, 0x77, 0x57};

    std::memcpy(&t[10], s2, sizeof s2);
    const uint8_t call[] = {0x90, 0x91, 0xE8, 0x10, 0x00, 0x00, 0x00};

    std::memcpy(&t[40], call, sizeof call);

    return t;
}

const RsxrSite kJmp{"J", "8B 75 0C 74 36 83 FA 05 77 57", 5, "83 FA 05 77 57", RsxrKind::Jmp, 5, 4, 1, 5};
const RsxrSite kCall{"C", "90 91 E8", 2, "E8 10 00 00 00", RsxrKind::Call, 5, 1, 4, 5};
}  // namespace

TEST(resolve_finds_sites_targets_and_original_bytes) {
    const auto t = Text();
    const RsxrSite sites[] = {kJmp, kCall};
    const auto r = Rsxr_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

    CHECK(r.ok);
    CHECK_EQ(r.sites.size(), size_t(2));
    CHECK_EQ(r.sites[0].addr, uintptr_t(0x401000 + 15));
    CHECK_EQ(r.sites[0].target, uintptr_t(0x401000 + 15 + 5 + 0x57));
    CHECK_EQ(r.sites[0].original.size(), size_t(5));
    CHECK_EQ(r.sites[1].addr, uintptr_t(0x401000 + 42));
    CHECK_EQ(r.sites[1].target, uintptr_t(0x401000 + 42 + 5 + 0x10));
}

TEST(missing_pattern_fails_everything) {
    const auto t = Text();
    const RsxrSite missing{"M", "DE AD BE EF", 0, "DE AD BE EF 00", RsxrKind::Jmp, 5, -1, 0, 0};
    const RsxrSite sites[] = {kJmp, missing};
    const auto r = Rsxr_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK_EQ(r.errors.size(), size_t(1));
    CHECK(r.errors[0].find("M:") == 0);
}

TEST(expect_mismatch_fails_everything) {
    auto t = Text();

    t[16] = 0xFB;  // as if another mod had patched the site
    // pattern covers only the bytes before the site, so it still matches once
    const RsxrSite j2{"J2", "8B 75 0C 74 36", 5, "83 FA 05 77 57", RsxrKind::Jmp, 5, 4, 1, 5};
    const RsxrSite sites[] = {j2, kCall};
    const auto r = Rsxr_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK_EQ(r.errors.size(), size_t(1));
    CHECK(r.errors[0].find("J2:") == 0);
    CHECK(r.errors[0].find("differ") != std::string::npos);
}

TEST(ref_site_value_is_the_dword_at_the_site) {
    const uint8_t text[] = {0x90, 0xA1, 0x00, 0x0B, 0x36, 0x01, 0xC3};
    const RsxrSite s{"X", "A1 ?? ?? ?? ?? C3", 1, "00 0B 36 01", RsxrKind::Ref, 4, -1, 0, 0};
    const auto r = Rsxr_ResolveAll(&s, 1, text, sizeof text, 0x1000);

    CHECK(r.ok);
    CHECK_EQ(r.sites[0].addr, uintptr_t(0x1002));
    CHECK_EQ(r.sites[0].target, uintptr_t(0x01360B00));
}

TEST(wildcard_in_expect_fails) {  // expect is the rollback data: a wildcard would restore 0x00
    const auto t = Text();
    const RsxrSite w{"W", "8B 75 0C 74 36 83 FA 05 77 57", 5, "83 ?? 05 77 57", RsxrKind::Jmp, 5, -1, 0, 0};
    const auto r = Rsxr_ResolveAll(&w, 1, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK(r.errors[0].find("wildcard") != std::string::npos);
}

TEST(site_before_text_fails) {  // a negative offset past the start must not wrap
    const auto t = Text();
    const RsxrSite neg{"N", "8B 75 0C 74 36 83 FA 05 77 57", -20, "00 00 00 00 00", RsxrKind::Jmp, 5, -1, 0, 0};
    const auto r = Rsxr_ResolveAll(&neg, 1, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK(r.errors[0].find("outside") != std::string::npos);
}

TEST(branch_field_past_text_fails) {
    const auto t = Text();
    const RsxrSite far{"F", "8B 75 0C 74 36 83 FA 05 77 57", 5, "83 FA 05 77 57", RsxrKind::Jmp, 5, 60, 4, 64};
    const auto r = Rsxr_ResolveAll(&far, 1, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK(r.errors[0].find("past .text") != std::string::npos);
}

TEST(two_matches_fail) {
    std::vector<uint8_t> t(32, 0x00);

    t[3] = t[13] = 0xCC;
    const RsxrSite dup{"D", "CC", 0, "CC 00 00 00 00", RsxrKind::Jmp, 5, -1, 0, 0};
    const auto r = Rsxr_ResolveAll(&dup, 1, t.data(), t.size(), 0);

    CHECK(!r.ok);
}

TEST(resolve_settles_on_the_same_few_failures) {
    RsxrSite sites[40] = {};
    RsxrResolveResult r{false, {}, {}, {}, {&sites[3]}};

    CHECK(!Rsxr_ResolveSettled(r, {}, 40));           // the first failing attempt: retry
    CHECK(Rsxr_ResolveSettled(r, {&sites[3]}, 40));   // the same site again: a foreign hook, not unpacking
    CHECK(!Rsxr_ResolveSettled(r, {&sites[4]}, 40));  // another site: still changing
    r.failed = {&sites[1], &sites[2], &sites[3]};
    CHECK(!Rsxr_ResolveSettled(r, r.failed, 40));  // more than 1 in 20 failing: .text may still be unpacking
    r.ok = true;
    r.failed.clear();
    CHECK(Rsxr_ResolveSettled(r, {}, 40));
}
