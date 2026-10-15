#include "core/colournative.h"
#include "core/extblock.h"
#include "core/fncopy.h"
#include "core/freeze.h"
#include "core/publish_state.h"
#include "core/sidestore.h"
#include "core/tuningname.h"
#include "test.h"
#include <windows.h>
#include <cstring>
#include <limits>
#include <memory>

TEST(review_fncopy_hash_known_vectors_and_exact_length) {
    const uint8_t word[] = {'f', 'o', 'o', 'b', 'a', 'r', 0xFF};

    CHECK_EQ(Rs7Fn_Fnv(nullptr, 0), 0x811C9DC5u);
    CHECK_EQ(Rs7Fn_Fnv(reinterpret_cast<const uint8_t*>("a"), 1), 0xE40C292Cu);
    CHECK_EQ(Rs7Fn_Fnv(word, 6), 0xBF9CF968u);
    CHECK(Rs7Fn_Fnv(word, 7) != Rs7Fn_Fnv(word, 6));
}

TEST(review_freeze_strict_interior_and_address_limits) {
    const uintptr_t max = (std::numeric_limits<uintptr_t>::max)();

    for (Rs7Range r : {Rs7Range{0, 4}, Rs7Range{0x1000, 0x1004}, Rs7Range{max - 4, max}}) {
        CHECK(!Rs7_FreezeRangeBusy(r, r.lo));
        CHECK(Rs7_FreezeRangeBusy(r, r.lo + 1));
        CHECK(Rs7_FreezeRangeBusy(r, r.hi - 1));
        CHECK(!Rs7_FreezeRangeBusy(r, r.hi));
        if (r.lo) {
            CHECK(!Rs7_FreezeRangeBusy(r, r.lo - 1));
        }
        if (r.hi < max) {
            CHECK(!Rs7_FreezeRangeBusy(r, r.hi + 1));
        }
    }

    for (uintptr_t ip : {uintptr_t(0), uintptr_t(1), uintptr_t(2), max}) {
        CHECK(!Rs7_FreezeRangeBusy({1, 1}, ip));
        CHECK(!Rs7_FreezeRangeBusy({2, 1}, ip));
        CHECK(!Rs7_FreezeRangeBusy({1, 2}, ip));  // no integer instruction address inside
    }
}

TEST(review_colour_layout_all_cells_are_contiguous_and_disjoint) {
    const uintptr_t saved = g_rs7_native_colours;

    for (uintptr_t enabled : {uintptr_t(0), uintptr_t(1)}) {
        g_rs7_native_colours = enabled;
        const unsigned width = enabled ? 8 : 6;
        const unsigned uv = enabled ? 0xA50 : 0x890;
        const unsigned size = enabled ? 0x1150 : 0xDD0;

        CHECK_EQ(rs7colour::Width(), width);
        unsigned cell = 0;

        for (unsigned mode = 0; mode < 2; ++mode) {
            for (unsigned state = 0; state < 28; ++state) {
                for (unsigned slot = 0; slot < width; ++slot, ++cell) {
                    CHECK_EQ(rs7colour::PtrOffset(mode, state, slot), 0x350u + 4 * cell);
                    CHECK_EQ(rs7colour::UVOffset(mode, state, slot), uv + 4 * cell);
                    CHECK(rs7colour::PtrOffset(mode, state, slot) + 4 <= uv);
                    CHECK(rs7colour::UVOffset(mode, state, slot) + 4 <= size);
                }
            }
        }

        CHECK_EQ(0x350u + 4 * cell, uv);
        CHECK_EQ(uv + 4 * cell, size);
        alignas(4) uint8_t mgr[rs7colour::kSize] = {};
        float sentinel[3] = {1, 0, 0};

        *reinterpret_cast<float**>(mgr + rs7colour::PtrOffset(1, 27, width - 1)) = sentinel;
        CHECK(rs7colour::GetRGB(mgr, 1, 27, width - 1) == sentinel);
        CHECK(rs7colour::GetRGB(mgr, 2, 0, 0) == nullptr);
        CHECK(rs7colour::GetRGB(mgr, 0, 28, 0) == nullptr);
        CHECK(rs7colour::GetRGB(mgr, 0, 0, width) == nullptr);
        CHECK(rs7colour::GetRGB(nullptr, 0, 0, 0) == nullptr);
    }

    g_rs7_native_colours = saved;
}

TEST(review_sidestore_full_update_erase_and_rollover) {
    Rs7Store_ClearAll();
    uint8_t keys[kRs7StoreMax + 1] = {};

    for (size_t i = 0; i < kRs7StoreMax; ++i) {
        Rs7Store_Set(keys + i, {true, int16_t(i)});
    }

    CHECK_EQ(Rs7Store_Count(), int(kRs7StoreMax));
    Rs7Store_Set(keys, {true, -12});  // replacing a key at capacity must not flush peers
    Rs7Entry out{false, 123};

    CHECK(Rs7Store_Get(keys, &out) && out.t6 == -12);
    CHECK(Rs7Store_Get(keys + 1, &out) && out.t6 == 1);
    CHECK_EQ(Rs7Store_Count(), int(kRs7StoreMax));
    Rs7Store_Set(keys + kRs7StoreMax, {false, 0});
    CHECK_EQ(Rs7Store_Count(), int(kRs7StoreMax));
    Rs7Store_Set(keys + 1, {false, 0});
    CHECK_EQ(Rs7Store_Count(), int(kRs7StoreMax - 1));
    Rs7Store_Set(keys + kRs7StoreMax, {true, -2});
    CHECK(Rs7Store_Get(keys, &out) && out.t6 == -12);
    Rs7Store_Set(keys + 1, {true, 7});  // a new key at capacity clears the whole old generation
    CHECK_EQ(Rs7Store_Count(), 1);
    CHECK(!Rs7Store_Get(keys, &out));
    CHECK(Rs7Store_Get(keys + 1, &out) && out.t6 == 7);
    out = {false, 123};
    CHECK(!Rs7Store_Get(keys, &out));
    CHECK(!out.active && out.t6 == 123);  // miss must leave the caller's output intact
    Rs7Store_ClearAll();
    CHECK(!Rs7Store_AnyActive());
}

TEST(review_extension_store_cap_refresh_eviction_and_shared_lifetime) {
    uint8_t keys[kRs7ExtKeep + 1] = {};
    auto first = std::make_shared<rs7ext::Block>();

    for (int i = 0; i < kRs7ExtKeep; ++i) {
        Rs7Ext_Store(keys + i, i == 0 ? first : std::make_shared<rs7ext::Block>());
    }

    CHECK_EQ(Rs7Ext_StoredBlocks(), kRs7ExtKeep);
    auto held = Rs7Ext_Find(keys + 1);

    CHECK(held != nullptr);
    Rs7Ext_Store(keys, first);  // refresh oldest key: key 1 is now the eviction candidate
    CHECK_EQ(Rs7Ext_StoredBlocks(), kRs7ExtKeep);
    Rs7Ext_Store(keys + kRs7ExtKeep, std::make_shared<rs7ext::Block>());
    CHECK(Rs7Ext_Find(keys) == first);
    CHECK(Rs7Ext_Find(keys + 1) == nullptr);
    CHECK(held != nullptr && held->stringCount == 0);  // an evicted block's borrower stays valid
    CHECK_EQ(Rs7Ext_StoredBlocks(), kRs7ExtKeep);

    for (int i = 0; i <= kRs7ExtKeep; ++i) {
        Rs7Ext_Store(keys + i, nullptr);
    }

    CHECK_EQ(Rs7Ext_StoredBlocks(), 0);
}

TEST(review_publication_lowest_invalid_width_falls_back_without_extra_reads) {
    const int16_t t[8] = {0, 0, 0, 0, 0, 0, -100, -100};

    for (unsigned n : {0u, 9u, (std::numeric_limits<unsigned>::max)()}) {
        CHECK_EQ(rs7pubstate::Lowest(t, n, false), 40);
    }

    for (unsigned n : {0u, 5u, 8u, (std::numeric_limits<unsigned>::max)()}) {
        CHECK_EQ(rs7pubstate::Lowest(t, n, true), 28);
    }
}

TEST(review_tuning_names_positive_eight_family_and_suffix_first_match) {
    CHECK(rs7tn::Name8(nullptr) == nullptr);
    int t[8] = {1, 1, 1, 1, 1, 1, 1, 1};

    CHECK(std::strcmp(rs7tn::Name8(t), "G Standard") == 0);
    t[7] = -1;
    CHECK(std::strcmp(rs7tn::Name8(t), "G Drop F") == 0);
    CHECK_EQ(rs7tn::Suffix8("7 of 7"), std::string("8 of 7"));
    CHECK_EQ(rs7tn::Display("Bb C# Db! aB"), std::string("B\xE2\x99\xAD C\xE2\x99\xAF DB! AB"));
}

namespace {
// Keep the null-input contract probe in-process without letting the access violation kill the suite.
bool Name7NullReturnsUnknown() {
    __try {
        return rs7tn::Name7(nullptr) == nullptr;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
}  // namespace

TEST(review_tuning_name7_null_matches_name8) {
    CHECK(Name7NullReturnsUnknown());
}

TEST(review_tuning_known_display7_respects_disabled_config) {
    const int b[7] = {};
    rs7tn::Config off;

    off.on = false;
    CHECK(rs7tn::KnownDisplay(b, off, -14, true).empty());
}
