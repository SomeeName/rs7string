#include "features/colours/colours.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "hook_rules.h"
#include "platform/midhook.h"
#include "test.h"
#include <windows.h>
#include <cstring>

TEST(native_colour_publication_keeps_stock_columns_and_owns_both_extra_palettes) {
    const auto saved = g_rs7_native_colours;

    g_rs7_native_colours = 1;
    alignas(4) uint8_t mgr[rs7colour::kSize] = {};
    float stock[2][28][6][3];

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                for (int c = 0; c < 3; ++c) {
                    stock[m][st][s][c] = 0.5f;
                }

                *reinterpret_cast<float**>(mgr + rs7colour::PtrOffset(m, st, s)) = stock[m][st][s];
                *reinterpret_cast<float*>(mgr + rs7colour::UVOffset(m, st, s)) = float(s) / 16;
            }
        }
    }

    uint8_t before[sizeof mgr];

    std::memcpy(before, mgr, sizeof mgr);
    float savedRgb[6];

    std::memcpy(savedRgb, g_rs7_fx_rgb, sizeof savedRgb);
    const float authored[6] = {0.8f, 0.6f, 0.4f, 0.1f, 0.2f, 0.3f};

    std::memcpy(g_rs7_fx_rgb, authored, sizeof authored);
    rs7colour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            CHECK(std::memcmp(before + rs7colour::PtrOffset(m, st), mgr + rs7colour::PtrOffset(m, st), 24) == 0);
            CHECK(std::memcmp(before + rs7colour::UVOffset(m, st), mgr + rs7colour::UVOffset(m, st), 24) == 0);

            for (int s = 6; s < 8; ++s) {
                CHECK(rs7colour::GetRGB(mgr, m, st, s) != nullptr);
            }

            CHECK(std::memcmp(rs7colour::GetRGB(mgr, m, 1, 6), authored + m * 3, 12) == 0);
            CHECK(std::memcmp(rs7colour::GetRGB(mgr, m, 1, 6), rs7colour::GetRGB(mgr, m, 1, 7), 12) != 0);
        }
    }

    const float* ptr = rs7colour::GetRGB(mgr, 0, 1, 6);

    g_rs7_fx_rgb[0] = 0.4f;
    rs7colour::Publish(mgr);
    CHECK(rs7colour::GetRGB(mgr, 0, 1, 6) == ptr && ptr[0] == 0.4f);
    std::memcpy(g_rs7_fx_rgb, savedRgb, sizeof savedRgb);
    g_rs7_native_colours = saved;
}

TEST(native_colour_off_does_not_publish_or_change_hook_selection) {
    const auto saved = g_rs7_native_colours;

    g_rs7_native_colours = 0;
    uint8_t mgr[rs7colour::kSize];

    std::memset(mgr, 0xCD, sizeof mgr);
    rs7colour::Publish(mgr);

    for (auto b : mgr) {
        CHECK(b == 0xCD);
    }

    CHECK(rs7colour::PtrOffset(1, 27, 5) == 0x88C);
    CHECK(rs7colour::UVOffset(1, 27, 5) == 0xDCC);

    for (bool strings : {false, true}) {
        CHECK(!Rs7_ShouldPatch(*Rs7_FindHook("CM518AC4"), true, true, true, strings));
    }

    g_rs7_native_colours = 1;

    for (bool strings : {false, true}) {
        CHECK(Rs7_ShouldPatch(*Rs7_FindHook("CM518AC4"), true, true, true, strings));
    }

    // 1.0: the stock-colour fallback sites (C1-C5, N11, GSCL) are gone.
    for (const char* id : {"C1", "C2", "C3", "C4", "C5", "N11", "GSCL"}) {
        CHECK(Rs7_FindHook(id) == nullptr);
    }

    g_rs7_native_colours = saved;
}

TEST(native_colour_scalar_callbacks_preserve_context_and_copy_bits) {
    alignas(4) uint8_t mgr[rs7colour::kSize] = {};
    uint32_t locals[16] = {};

    locals[5] = 0x3EAAAAAB;
    locals[3] = 0xBF800000;
    Rs7Mid::Context c = {};

    c.ecx = uint32_t(mgr);
    c.ebp = uint32_t(locals);
    c.eax = 40;
    c.edx = 42;
    c.eflags = 0x8D7;
    Rs7Colour_CMUV(c);

    for (int s : {0, 6, 7}) {
        CHECK_EQ(*reinterpret_cast<uint32_t*>(mgr + rs7colour::kUV + 4 * (40 + s)), locals[5]);
    }

    CHECK_EQ(c.eax, 40u);
    CHECK_EQ(c.eflags, 0x8D7u);
    CHECK_EQ(c.jump, 0x518BC7u);
    Rs7Colour_CMIX(c);
    CHECK_EQ(c.eax, 336u);
    CHECK_EQ(c.eflags, 0x8D7u);

    for (int s : {6, 7}) {
        CHECK_EQ(*reinterpret_cast<uint32_t*>(mgr + rs7colour::kUV + 4 * (336 + s)), locals[3]);
    }
}

TEST(native_colour_foreign_thread_publish_waits_for_the_built_manager) {
    // R2b: the ramp thread published into a manager the game thread was still building -> AV, hang.
    const auto savedOn = g_rs7_native_colours, savedRef = g_rs7_colormgr;

    rs7colour::ResetThreadForTests();
    g_rs7_native_colours = 1;
    alignas(4) static uint8_t mgr[rs7colour::kSize];

    std::memset(mgr, 0, sizeof mgr);
    static uint8_t* ref = mgr;

    g_rs7_colormgr = reinterpret_cast<uintptr_t>(&ref);
    Rs7Mid::Context c = {};

    c.esi = uint32_t(uintptr_t(mgr));
    Rs7Colour_CMINIT(c);                 // ctor end: not built yet
    std::memset(mgr, 0xCD, sizeof mgr);  // half-built: garbage slot pointers
    rs7colour::PublishCurrent();         // must not dereference the 0xCDCDCDCD slot pointers
    CHECK(mgr[0] == 0xCD && mgr[100] == 0xCD);
    std::memset(mgr, 0, sizeof mgr);  // build finished
    Rs7Colour_CMBUILD(c);             // publishes and marks the manager built
    const uint32_t before = *reinterpret_cast<uint32_t*>(mgr + rs7colour::PtrOffset(0, 1, 6));

    CHECK(before != 0);
    *reinterpret_cast<uint32_t*>(mgr + rs7colour::PtrOffset(0, 1, 6)) = 0;
    rs7colour::PublishCurrent();  // built: foreign threads may publish again
    CHECK(*reinterpret_cast<uint32_t*>(mgr + rs7colour::PtrOffset(0, 1, 6)) == before);
    g_rs7_native_colours = savedOn;
    g_rs7_colormgr = savedRef;
}

namespace {
struct CmFixture {
    alignas(4) static inline uint8_t mgr[rs7colour::kSize];
    static inline uint8_t* ref = nullptr;
    uintptr_t savedOn, savedRef;
    uint32_t published = 0;

    CmFixture() : savedOn(g_rs7_native_colours), savedRef(g_rs7_colormgr) {
        rs7colour::ResetThreadForTests();
        g_rs7_native_colours = 1;
        std::memset(mgr, 0, sizeof mgr);
        ref = mgr;
        g_rs7_colormgr = reinterpret_cast<uintptr_t>(&ref);
        Rs7Mid::Context c = {};

        c.esi = uint32_t(uintptr_t(mgr));
        Rs7Colour_CMINIT(c);
        Rs7Colour_CMBUILD(c);  // this (test) thread is the game thread now
        published = Slot();
        Slot() = 0;
    }

    ~CmFixture() {
        g_rs7_native_colours = savedOn;
        g_rs7_colormgr = savedRef;
        rs7colour::ResetThreadForTests();
    }

    static uint32_t& Slot() { return *reinterpret_cast<uint32_t*>(mgr + rs7colour::PtrOffset(0, 1, 6)); }

    static void OnOtherThread(void (*fn)()) {
        const HANDLE t = CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD {
                reinterpret_cast<void (*)()>(p)();

                return 0;
            },
            reinterpret_cast<LPVOID>(fn), 0, nullptr);

        WaitForSingleObject(t, 5000);
        CloseHandle(t);
    }
};
}  // namespace

TEST(native_colour_foreign_publish_is_deferred_to_the_game_thread) {
    // Review #5: only the thread that ran CMBUILD writes into the manager.
    CmFixture f;

    CHECK(f.published != 0);
    CmFixture::OnOtherThread([] { rs7colour::PublishCurrent(); });  // the overlay on a foreign thread
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rs7colour::PublishPending());
    CmFixture::OnOtherThread([] { rs7colour::DrainPending(); });  // a drain elsewhere does nothing
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rs7colour::PublishPending());
    rs7colour::DrainPending();  // the game thread (FGHK / SetTexture)
    CHECK_EQ(CmFixture::Slot(), f.published);
    CHECK(!rs7colour::PublishPending());
}

TEST(ramp_v6_store_never_touches_the_colour_manager) {
    // Review #5: StoreV6 runs on the ramp worker; it may only request a publish.
    CmFixture f;

    CmFixture::OnOtherThread([] { Rs7Cand_ResetForTests(); });  // StoreV6(false) on a foreign thread
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rs7colour::PublishPending());
    Rs7Cand_ResetForTests();  // on the game thread too: still only a request
    CHECK_EQ(CmFixture::Slot(), 0u);
    rs7colour::DrainPending();
    CHECK_EQ(CmFixture::Slot(), f.published);
}

TEST(native_colour_cmbuild_drains_a_request_made_before_the_build) {
    CmFixture f;

    rs7colour::ResetThreadForTests();  // a new song: no manager thread yet
    rs7colour::RequestPublish();
    rs7colour::DrainPending();  // no CMBUILD thread known: stays pending
    CHECK_EQ(CmFixture::Slot(), 0u);
    Rs7Mid::Context c = {};

    c.esi = uint32_t(uintptr_t(CmFixture::mgr));
    Rs7Colour_CMBUILD(c);
    CHECK_EQ(CmFixture::Slot(), f.published);
    CHECK(!rs7colour::PublishPending());
}

// RSMods GetStringColor uses mode*0xA8 and the stock state offsets, including in CB mode.
TEST(native_colour_rsmods_six_column_abi_shares_the_correct_rgb_values) {
    const auto saved = g_rs7_native_colours;

    g_rs7_native_colours = 1;
    alignas(4) uint8_t mgr[rs7colour::kSize] = {};
    float stock[2][28][6][3] = {};

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                stock[m][st][s][0] = float(m * 168 + st * 6 + s);
                *reinterpret_cast<float*>(mgr + rs7colour::UVOffset(m, st, s)) = float(m * 168 + st * 6 + s);
                *reinterpret_cast<float**>(mgr + rs7colour::PtrOffset(m, st, s)) = stock[m][st][s];
            }
        }
    }

    rs7colour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                auto* legacy = *reinterpret_cast<float**>(mgr + 0x350 + 4 * (m * 0xA8 + st * 6 + s));

                CHECK(legacy == rs7colour::GetRGB(mgr, m, st, s));
                CHECK_EQ(*reinterpret_cast<float*>(mgr + 0x890 + 4 * (m * 0xA8 + st * 6 + s)),
                         float(m * 168 + st * 6 + s));

                if (legacy == stock[m][st][s]) {
                    legacy[1] = 0.25f;  // custom/rainbow write through RSMods' address
                    CHECK_EQ(rs7colour::GetRGB(mgr, m, st, s)[1], 0.25f);
                }
            }
        }
    }

    g_rs7_native_colours = saved;
}

extern "C" void __cdecl Rs7_CallIn_GlowColour(Rs7Mid::Context& c);

TEST(native_colour_glow_callin_reads_all_eight_slots_in_both_modes) {
    const auto saved = g_rs7_native_colours;

    g_rs7_native_colours = 1;
    alignas(4) uint8_t mgr[rs7colour::kSize] = {};
    float colours[2][8][3] = {};

    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t slot = 0; slot < 8; ++slot) {
            *reinterpret_cast<float**>(mgr + rs7colour::PtrOffset(mode, 3, slot)) = colours[mode][slot];
            Rs7Mid::Context c{};

            c.ecx = uint32_t(reinterpret_cast<uintptr_t>(mgr));
            c.eax = mode * 0xA8 + slot;
            c.edx = slot;
            Rs7_CallIn_GlowColour(c);
            const auto* rgbPtr = reinterpret_cast<float*>(uintptr_t(c.eax));

            CHECK(rgbPtr == colours[mode][slot]);
        }
    }

    g_rs7_native_colours = saved;
}
