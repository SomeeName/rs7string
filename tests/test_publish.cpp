#include "core/config.h"
#include "core/publish.h"
#include "hooks/registry.h"
#include "test.h"
#include <cstdint>
#include <cstring>

TEST(publish_missing_each_prerequisite_blocks_ports) {
    for (unsigned m = 0; m < 32; ++m) {
        CHECK_EQ(rs7pub::Missing(m), 31u & ~m);
        CHECK_EQ(rs7pub::PortsEnabled(true, m), m == 31);
        CHECK(!rs7pub::PortsEnabled(false, m));
    }
}

TEST(publish_audit_gate_is_ready) {
    CHECK(rs7pub::PublicationReady());  // 1.0: [neck] native_publish is gone, the ports are a native unit member
}

TEST(publish_classification_exact_active_count_and_candidate_order) {
    const int16_t t[8] = {0, 0, 0, 0, 0, 0, -1, -2};
    uint8_t r[0xC2] = {};

    r[0xC0] = 0;
    r[0xC1] = 5;
    CHECK_EQ(rs7pub::Classify(r, t, 6, 37, 7), 7);
    CHECK_EQ(rs7pub::Classify(r, t, 7, 39, 7), 2);  // 35 - 1 + 5
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x18), 6);
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x14), 5);
    CHECK_EQ(rs7pub::Classify(r, t, 8, 33, 7), 2);  // 30 - 2 + 5
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x18), 7);
    CHECK_EQ(rs7pub::Classify(r, t, 7, 33, 7), 7);  // slot 7 MUST NOT be read
    CHECK_EQ(rs7pub::Classify(r, t, 8, 34, 7), 1);
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x14), 6);
}

TEST(publish_classification_preserves_no_pitch_early_exit_and_clamp) {
    const int16_t t[8] = {};
    uint8_t r[0xC2] = {};

    r[0xC0] = 0;
    r[0xC1] = 0;
    CHECK_EQ(rs7pub::Classify(r, t, 8, 35, 3), 3);  // first non-skipped failure exits
    CHECK_EQ(rs7pub::Classify(r, t, 8, 33, 7), 1);
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x18), 6);
    CHECK_EQ(*reinterpret_cast<int*>(r + 0x14), 0);
}

TEST(publish_invalid_count_never_scans_past_capacity) {
    const int16_t t[8] = {};
    uint8_t r[0xC2] = {};

    r[0xC0] = 0;
    CHECK_EQ(rs7pub::Classify(r, t, 9, 30, 7), 7);
    CHECK_EQ(rs7pub::Classify(r, t, 0, 30, 7), 7);
}

TEST(publish_registry_selects_exactly_one_nextgrid_guard_variant) {
    const uintptr_t saved = g_rs7_publish_ports;

    for (const char* id : {"NN1", "NN3", "NN4"}) {
        const auto* stock = Rs7_FindHook(id);
        const char* native = !std::strcmp(id, "NN1") ? "PUBNN1" : !std::strcmp(id, "NN3") ? "PUBNN3" : "PUBNN4";
        const auto* port = Rs7_FindHook(native);

        CHECK(stock && port);
        if (!stock || !port) {
            continue;
        }

        g_rs7_publish_ports = 0;
        CHECK(Rs7_ShouldPatch(*stock, false));
        CHECK(!Rs7_ShouldPatch(*port, true, true, true, true));
        g_rs7_publish_ports = 1;
        CHECK(!Rs7_ShouldPatch(*stock, true));
        CHECK(Rs7_ShouldPatch(*port, false));
    }

    g_rs7_publish_ports = saved;
}
