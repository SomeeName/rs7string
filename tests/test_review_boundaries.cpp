#include "features/colours/colours.h"
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/publish/publish.h"
#include "features/tuner/tuningname.h"
#include "platform/fncopy.h"
#include "platform/freeze.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstring>
#include <limits>
#include <memory>

TEST(review_fncopy_hash_known_vectors_and_exact_length) {
    const uint8_t word[] = {'f', 'o', 'o', 'b', 'a', 'r', 0xFF};

    CHECK_EQ(RsxrFn_Fnv(nullptr, 0), 0x811C9DC5u);
    CHECK_EQ(RsxrFn_Fnv(reinterpret_cast<const uint8_t*>("a"), 1), 0xE40C292Cu);
    CHECK_EQ(RsxrFn_Fnv(word, 6), 0xBF9CF968u);
    CHECK(RsxrFn_Fnv(word, 7) != RsxrFn_Fnv(word, 6));
}

TEST(review_freeze_strict_interior_and_address_limits) {
    const uintptr_t max = (std::numeric_limits<uintptr_t>::max)();

    for (RsxrRange r : {RsxrRange{0, 4}, RsxrRange{0x1000, 0x1004}, RsxrRange{max - 4, max}}) {
        CHECK(!Rsxr_FreezeRangeBusy(r, r.lo));
        CHECK(Rsxr_FreezeRangeBusy(r, r.lo + 1));
        CHECK(Rsxr_FreezeRangeBusy(r, r.hi - 1));
        CHECK(!Rsxr_FreezeRangeBusy(r, r.hi));
        if (r.lo) {
            CHECK(!Rsxr_FreezeRangeBusy(r, r.lo - 1));
        }
        if (r.hi < max) {
            CHECK(!Rsxr_FreezeRangeBusy(r, r.hi + 1));
        }
    }

    for (uintptr_t ip : {uintptr_t(0), uintptr_t(1), uintptr_t(2), max}) {
        CHECK(!Rsxr_FreezeRangeBusy({1, 1}, ip));
        CHECK(!Rsxr_FreezeRangeBusy({2, 1}, ip));
        CHECK(!Rsxr_FreezeRangeBusy({1, 2}, ip));  // no integer instruction address inside
    }
}

TEST(review_colour_layout_all_cells_are_contiguous_and_disjoint) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    for (uintptr_t enabled : {uintptr_t(0), uintptr_t(1)}) {
        g_rsxr_native_colours = enabled;
        const unsigned width = enabled ? 8 : 6;
        const unsigned ptr = enabled ? rsxrcolour::kPtr : 0x350;
        const unsigned uv = enabled ? rsxrcolour::kUV : 0x890;
        const unsigned size = enabled ? rsxrcolour::kSize : 0xDD0;

        CHECK_EQ(rsxrcolour::Width(), width);
        unsigned cell = 0;

        for (unsigned mode = 0; mode < 2; ++mode) {
            for (unsigned state = 0; state < 28; ++state) {
                for (unsigned slot = 0; slot < width; ++slot, ++cell) {
                    CHECK_EQ(rsxrcolour::PtrOffset(mode, state, slot), ptr + 4 * cell);
                    CHECK_EQ(rsxrcolour::UVOffset(mode, state, slot), uv + 4 * cell);
                    CHECK(rsxrcolour::PtrOffset(mode, state, slot) + 4 <= uv);
                    CHECK(rsxrcolour::UVOffset(mode, state, slot) + 4 <= size);
                }
            }
        }

        CHECK_EQ(ptr + 4 * cell, uv);
        CHECK_EQ(uv + 4 * cell, size);
        alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
        float sentinel[3] = {1, 0, 0};

        *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(1, 27, width - 1)) = sentinel;
        CHECK(rsxrcolour::GetRGB(mgr, 1, 27, width - 1) == sentinel);
        CHECK(rsxrcolour::GetRGB(mgr, 2, 0, 0) == nullptr);
        CHECK(rsxrcolour::GetRGB(mgr, 0, 28, 0) == nullptr);
        CHECK(rsxrcolour::GetRGB(mgr, 0, 0, width) == nullptr);
        CHECK(rsxrcolour::GetRGB(nullptr, 0, 0, 0) == nullptr);
    }
}

TEST(review_sidestore_full_update_erase_and_rollover) {
    RsxrStore_ClearAll();
    uint8_t keys[kRsxrStoreMax + 1] = {};

    for (size_t i = 0; i < kRsxrStoreMax; ++i) {
        RsxrStore_Set(keys + i, {true, int16_t(i)});
    }

    CHECK_EQ(RsxrStore_Count(), int(kRsxrStoreMax));
    RsxrStore_Set(keys, {true, -12});  // replacing a key at capacity must not flush peers
    RsxrEntry out{false, 123};

    CHECK(RsxrStore_Get(keys, &out) && out.t6 == -12);
    CHECK(RsxrStore_Get(keys + 1, &out) && out.t6 == 1);
    CHECK_EQ(RsxrStore_Count(), int(kRsxrStoreMax));
    RsxrStore_Set(keys + kRsxrStoreMax, {false, 0});
    CHECK_EQ(RsxrStore_Count(), int(kRsxrStoreMax));
    RsxrStore_Set(keys + 1, {false, 0});
    CHECK_EQ(RsxrStore_Count(), int(kRsxrStoreMax - 1));
    RsxrStore_Set(keys + kRsxrStoreMax, {true, -2});
    CHECK(RsxrStore_Get(keys, &out) && out.t6 == -12);
    RsxrStore_Set(keys + 1, {true, 7});  // a new key at capacity clears the whole old generation
    CHECK_EQ(RsxrStore_Count(), 1);
    CHECK(!RsxrStore_Get(keys, &out));
    CHECK(RsxrStore_Get(keys + 1, &out) && out.t6 == 7);
    out = {false, 123};
    CHECK(!RsxrStore_Get(keys, &out));
    CHECK(!out.active && out.t6 == 123);  // miss must leave the caller's output intact
    RsxrStore_ClearAll();
    CHECK(!RsxrStore_AnyActive());
}

TEST(review_extension_store_cap_refresh_eviction_and_shared_lifetime) {
    uint8_t keys[kRsxrExtKeep + 1] = {};
    auto first = std::make_shared<rsxrext::Block>();

    for (int i = 0; i < kRsxrExtKeep; ++i) {
        RsxrExt_Store(keys + i, i == 0 ? first : std::make_shared<rsxrext::Block>());
    }

    CHECK_EQ(RsxrExt_StoredBlocks(), kRsxrExtKeep);
    auto held = RsxrExt_Find(keys + 1);

    CHECK(held != nullptr);
    RsxrExt_Store(keys, first);  // refresh oldest key: key 1 is now the eviction candidate
    CHECK_EQ(RsxrExt_StoredBlocks(), kRsxrExtKeep);
    RsxrExt_Store(keys + kRsxrExtKeep, std::make_shared<rsxrext::Block>());
    CHECK(RsxrExt_Find(keys) == first);
    CHECK(RsxrExt_Find(keys + 1) == nullptr);
    CHECK(held != nullptr && held->stringCount == 0);  // an evicted block's borrower stays valid
    CHECK_EQ(RsxrExt_StoredBlocks(), kRsxrExtKeep);

    for (int i = 0; i <= kRsxrExtKeep; ++i) {
        RsxrExt_Store(keys + i, nullptr);
    }

    CHECK_EQ(RsxrExt_StoredBlocks(), 0);
}

TEST(review_publication_lowest_invalid_width_falls_back_without_extra_reads) {
    const int16_t t[8] = {0, 0, 0, 0, 0, 0, -100, -100};

    for (unsigned n : {0u, 9u, (std::numeric_limits<unsigned>::max)()}) {
        CHECK_EQ(rsxrpubstate::Lowest(t, n, false), 40);
    }

    for (unsigned n : {0u, 5u, 8u, (std::numeric_limits<unsigned>::max)()}) {
        CHECK_EQ(rsxrpubstate::Lowest(t, n, true), 28);
    }
}

TEST(review_tuning_names_positive_eight_family_and_suffix_first_match) {
    CHECK(rsxrtn::Name8(nullptr) == nullptr);
    int t[8] = {1, 1, 1, 1, 1, 1, 1, 1};

    CHECK(std::strcmp(rsxrtn::Name8(t), "G Standard") == 0);
    t[7] = -1;
    CHECK(std::strcmp(rsxrtn::Name8(t), "G Drop F") == 0);
    CHECK_EQ(rsxrtn::SuffixN("7 of 7", 8), std::string("8 of 7"));
    CHECK_EQ(rsxrtn::Display("Bb C# Db! aB"), std::string("B\xE2\x99\xAD C\xE2\x99\xAF DB! AB"));
}

namespace {
// Keep the null-input contract probe in-process without letting the access violation kill the suite.
bool Name7NullReturnsUnknown() {
    __try {
        return rsxrtn::Name7(nullptr) == nullptr;
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
    rsxrtn::Config off;

    off.on = false;
    CHECK(rsxrtn::KnownDisplay(b, off, -14, true).empty());
}
