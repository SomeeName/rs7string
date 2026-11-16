// Rs7Spacing_Record / _Forget / _Factor: the per-instrument spacing registry (8 slots, round robin).
#include "features/spacing/spacing.h"
#include "test.h"
#include <atomic>
#include <thread>

namespace {
uint8_t g_infos[16];  // distinct instrument infos; only their addresses matter
}

TEST(spacing_registry_unknown_and_null_give_one) {
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[0]), 1.0f);
    CHECK_EQ(Rs7Spacing_Factor(nullptr), 1.0f);
}

TEST(spacing_registry_records_and_updates_in_place) {
    Rs7Spacing_Record(&g_infos[1], 2.0f);
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[1]), 2.0f);

    Rs7Spacing_Record(&g_infos[1], 3.0f);
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[1]), 3.0f);

    // The update took no new slot: seven other instruments leave it in place.
    for (int i = 2; i < 9; ++i) {
        Rs7Spacing_Record(&g_infos[i], 1.5f + float(i));
    }

    CHECK_EQ(Rs7Spacing_Factor(&g_infos[1]), 3.0f);
}

TEST(spacing_registry_ninth_instrument_drops_the_oldest) {
    // Round robin: the ninth new instrument takes the slot of the first of the eight before it.
    for (int i = 0; i < 9; ++i) {
        Rs7Spacing_Record(&g_infos[i], 2.0f + float(i));
    }

    CHECK_EQ(Rs7Spacing_Factor(&g_infos[0]), 1.0f);  // dropped

    for (int i = 1; i < 9; ++i) {
        CHECK_EQ(Rs7Spacing_Factor(&g_infos[i]), 2.0f + float(i));
    }
}

TEST(spacing_registry_forget_resets_to_one) {
    Rs7Spacing_Record(&g_infos[10], 2.5f);
    Rs7Spacing_Record(&g_infos[11], 1.75f);
    Rs7Spacing_Forget(&g_infos[10]);
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[10]), 1.0f);
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[11]), 1.75f);  // the others keep theirs

    Rs7Spacing_Forget(&g_infos[12]);  // never recorded: no effect
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[12]), 1.0f);
}

TEST(spacing_registry_null_is_ignored) {
    Rs7Spacing_Record(&g_infos[13], 2.25f);
    Rs7Spacing_Record(nullptr, 9.0f);
    Rs7Spacing_Forget(nullptr);
    CHECK_EQ(Rs7Spacing_Factor(nullptr), 1.0f);
    CHECK_EQ(Rs7Spacing_Factor(&g_infos[13]), 2.25f);
}

TEST(spacing_registry_concurrent_replacement_keeps_each_factor_with_its_info) {
    uint8_t infos[32] = {};

    for (const auto& info : infos) {
        Rs7Spacing_Forget(&info);
    }

    std::atomic<bool> start{false}, done{false};
    std::thread writer([&] {
        while (!start.load()) {
            std::this_thread::yield();
        }

        for (unsigned pass = 0; pass < 20000; ++pass) {
            for (unsigned i = 0; i < 32; ++i) {
                Rs7Spacing_Record(&infos[i], 2.0f + float(i));
            }
        }

        done.store(true);
    });
    bool consistent = true;
    unsigned reads = 0;

    start.store(true);

    do {
        for (unsigned i = 0; i < 32; ++i) {
            const float k = Rs7Spacing_Factor(&infos[i]);

            consistent = consistent && (k == 1.0f || k == 2.0f + float(i));
            ++reads;
        }
    } while (!done.load());

    writer.join();
    CHECK(reads > 0);
    CHECK(consistent);

    for (const auto& info : infos) {
        Rs7Spacing_Forget(&info);
    }
}
