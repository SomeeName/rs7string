#include "core/resolve.h"
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

const Rs7Site kJmp{"J", "8B 75 0C 74 36 83 FA 05 77 57", 5, "83 FA 05 77 57", Rs7Kind::Jmp, 5, 4, 1, 5};
const Rs7Site kCall{"C", "90 91 E8", 2, "E8 10 00 00 00", Rs7Kind::Call, 5, 1, 4, 5};
}  // namespace

TEST(resolve_finds_sites_targets_and_original_bytes) {
    const auto t = Text();
    const Rs7Site sites[] = {kJmp, kCall};
    const auto r = Rs7_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

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
    const Rs7Site missing{"M", "DE AD BE EF", 0, "DE AD BE EF 00", Rs7Kind::Jmp, 5, -1, 0, 0};
    const Rs7Site sites[] = {kJmp, missing};
    const auto r = Rs7_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK_EQ(r.errors.size(), size_t(1));
    CHECK(r.errors[0].find("M:") == 0);
}

TEST(expect_mismatch_fails_everything) {
    auto t = Text();

    t[16] = 0xFB;  // as if another mod had patched the site
    // pattern covers only the bytes before the site, so it still matches once
    const Rs7Site j2{"J2", "8B 75 0C 74 36", 5, "83 FA 05 77 57", Rs7Kind::Jmp, 5, 4, 1, 5};
    const Rs7Site sites[] = {j2, kCall};
    const auto r = Rs7_ResolveAll(sites, 2, t.data(), t.size(), 0x401000);

    CHECK(!r.ok);
    CHECK_EQ(r.errors.size(), size_t(1));
    CHECK(r.errors[0].find("J2:") == 0);
    CHECK(r.errors[0].find("differ") != std::string::npos);
}

TEST(ref_site_value_is_the_dword_at_the_site) {
    const uint8_t text[] = {0x90, 0xA1, 0x00, 0x0B, 0x36, 0x01, 0xC3};
    const Rs7Site s{"X", "A1 ?? ?? ?? ?? C3", 1, "00 0B 36 01", Rs7Kind::Ref, 4, -1, 0, 0};
    const auto r = Rs7_ResolveAll(&s, 1, text, sizeof text, 0x1000);

    CHECK(r.ok);
    CHECK_EQ(r.sites[0].addr, uintptr_t(0x1002));
    CHECK_EQ(r.sites[0].target, uintptr_t(0x01360B00));
}

TEST(two_matches_fail) {
    std::vector<uint8_t> t(32, 0x00);

    t[3] = t[13] = 0xCC;
    const Rs7Site dup{"D", "CC", 0, "CC 00 00 00 00", Rs7Kind::Jmp, 5, -1, 0, 0};
    const auto r = Rs7_ResolveAll(&dup, 1, t.data(), t.size(), 0);

    CHECK(!r.ok);
}
