#include "features/colours/colours.h"
#include "features/colours/state.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "hook_rules.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cstring>
#include <vector>

TEST(native_colour_publication_keeps_stock_columns_and_owns_both_extra_palettes) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 1;
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    float stock[2][28][6][3];

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                for (int c = 0; c < 3; ++c) {
                    stock[m][st][s][c] = 0.5f;
                }

                *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(m, st, s)) = stock[m][st][s];
                *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, st, s)) = float(s) / 16;
            }
        }
    }

    const std::vector<uint8_t> before(mgr, mgr + sizeof mgr);  // on the heap: two managers would pass 16 KB of stack
    float savedRgb[6];

    std::memcpy(savedRgb, g_rsxr_fx_rgb, sizeof savedRgb);
    const float authored[6] = {0.8f, 0.6f, 0.4f, 0.1f, 0.2f, 0.3f};

    std::memcpy(g_rsxr_fx_rgb, authored, sizeof authored);
    rsxrcolour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            CHECK(std::memcmp(before.data() + rsxrcolour::PtrOffset(m, st), mgr + rsxrcolour::PtrOffset(m, st), 24) ==
                  0);
            CHECK(std::memcmp(before.data() + rsxrcolour::UVOffset(m, st), mgr + rsxrcolour::UVOffset(m, st), 24) == 0);

            for (int s = 6; s < 8; ++s) {
                CHECK(rsxrcolour::GetRGB(mgr, m, st, s) != nullptr);
            }

            CHECK(std::memcmp(rsxrcolour::GetRGB(mgr, m, 1, 6), authored + m * 3, 12) == 0);
            CHECK(std::memcmp(rsxrcolour::GetRGB(mgr, m, 1, 6), rsxrcolour::GetRGB(mgr, m, 1, 7), 12) != 0);
        }
    }

    const float* ptr = rsxrcolour::GetRGB(mgr, 0, 1, 6);

    g_rsxr_fx_rgb[0] = 0.4f;
    rsxrcolour::Publish(mgr);
    CHECK(rsxrcolour::GetRGB(mgr, 0, 1, 6) == ptr && ptr[0] == 0.4f);
    std::memcpy(g_rsxr_fx_rgb, savedRgb, sizeof savedRgb);
}

TEST(native_colour_off_does_not_publish_or_change_hook_selection) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 0;
    uint8_t mgr[rsxrcolour::kSize];

    std::memset(mgr, 0xCD, sizeof mgr);
    rsxrcolour::Publish(mgr);

    for (auto b : mgr) {
        CHECK(b == 0xCD);
    }

    CHECK(rsxrcolour::PtrOffset(1, 27, 5) == 0x88C);
    CHECK(rsxrcolour::UVOffset(1, 27, 5) == 0xDCC);

    for (bool strings : {false, true}) {
        CHECK(!Rsxr_ShouldPatch(*Rsxr_FindHook("CM518AC4"), true, true, true, strings));
    }

    g_rsxr_native_colours = 1;

    for (bool strings : {false, true}) {
        CHECK(Rsxr_ShouldPatch(*Rsxr_FindHook("CM518AC4"), true, true, true, strings));
    }

    // 1.0: the stock-colour fallback sites (C1-C5, N11, GSCL) are gone.
    for (const char* id : {"C1", "C2", "C3", "C4", "C5", "N11", "GSCL"}) {
        CHECK(Rsxr_FindHook(id) == nullptr);
    }
}

TEST(native_colour_scalar_callbacks_preserve_context_and_copy_bits) {
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    uint32_t locals[16] = {};

    locals[5] = 0x3EAAAAAB;
    locals[3] = 0xBF800000;
    RsxrMid::Context c = {};

    c.ecx = uint32_t(mgr);
    c.ebp = uint32_t(locals);
    c.eax = 40;
    c.edx = 42;
    c.eflags = 0x8D7;
    RsxrColour_CMUV(c);

    for (int s : {0, 6, 7}) {
        CHECK_EQ(*reinterpret_cast<uint32_t*>(mgr + rsxrcolour::kUV + 4 * (40 + s)), locals[5]);
    }

    CHECK_EQ(c.eax, 40u);
    CHECK_EQ(c.eflags, 0x8D7u);
    CHECK_EQ(c.jump, 0x518BC7u);
    RsxrColour_CMIX(c);
    CHECK_EQ(c.eax, 336u);
    CHECK_EQ(c.eflags, 0x8D7u);

    for (int s : {6, 7}) {
        CHECK_EQ(*reinterpret_cast<uint32_t*>(mgr + rsxrcolour::kUV + 4 * (336 + s)), locals[3]);
    }
}

TEST(native_colour_foreign_thread_publish_waits_for_the_built_manager) {
    // R2b: the ramp thread published into a manager the game thread was still building -> AV, hang.
    const auto savedOn = g_rsxr_native_colours, savedRef = g_rsxr_colormgr;

    rsxrcolour::ResetThreadForTests();
    g_rsxr_native_colours = 1;
    alignas(4) static uint8_t mgr[rsxrcolour::kSize];

    std::memset(mgr, 0, sizeof mgr);
    static uint8_t* ref = mgr;

    g_rsxr_colormgr = reinterpret_cast<uintptr_t>(&ref);
    RsxrMid::Context c = {};

    c.esi = uint32_t(uintptr_t(mgr));
    RsxrColour_CMINIT(c);                // ctor end: not built yet
    std::memset(mgr, 0xCD, sizeof mgr);  // half-built: garbage slot pointers
    rsxrcolour::PublishCurrent();        // must not dereference the 0xCDCDCDCD slot pointers
    CHECK(mgr[0] == 0xCD && mgr[100] == 0xCD);
    std::memset(mgr, 0, sizeof mgr);  // build finished
    RsxrColour_CMBUILD(c);            // publishes and marks the manager built
    const uint32_t before = *reinterpret_cast<uint32_t*>(mgr + rsxrcolour::PtrOffset(0, 1, 6));

    CHECK(before != 0);
    *reinterpret_cast<uint32_t*>(mgr + rsxrcolour::PtrOffset(0, 1, 6)) = 0;
    rsxrcolour::PublishCurrent();  // built: the manager thread may publish immediately
    CHECK(*reinterpret_cast<uint32_t*>(mgr + rsxrcolour::PtrOffset(0, 1, 6)) == before);
    g_rsxr_native_colours = savedOn;
    g_rsxr_colormgr = savedRef;
}

namespace {
struct CmFixture {
    alignas(4) static inline uint8_t mgr[rsxrcolour::kSize];
    static inline uint8_t* ref = nullptr;
    uintptr_t savedOn, savedRef;
    uint32_t published = 0;

    CmFixture() : savedOn(g_rsxr_native_colours), savedRef(g_rsxr_colormgr) {
        rsxrcolour::ResetThreadForTests();
        g_rsxr_native_colours = 1;
        std::memset(mgr, 0, sizeof mgr);
        ref = mgr;
        g_rsxr_colormgr = reinterpret_cast<uintptr_t>(&ref);
        RsxrMid::Context c = {};

        c.esi = uint32_t(uintptr_t(mgr));
        RsxrColour_CMINIT(c);
        RsxrColour_CMBUILD(c);  // this (test) thread is the game thread now
        published = Slot();
        Slot() = 0;
    }

    ~CmFixture() {
        g_rsxr_native_colours = savedOn;
        g_rsxr_colormgr = savedRef;
        rsxrcolour::ResetThreadForTests();
    }

    static uint32_t& Slot() { return *reinterpret_cast<uint32_t*>(mgr + rsxrcolour::PtrOffset(0, 1, 6)); }

    static void OnOtherThread(void (*fn)()) {
        const LPTHREAD_START_ROUTINE run = [](LPVOID p) -> DWORD {
            reinterpret_cast<void (*)()>(p)();

            return 0;
        };
        const HANDLE t =
            rsxrtest::Must(CreateThread(nullptr, 0, run, reinterpret_cast<LPVOID>(fn), 0, nullptr), "CreateThread");

        rsxrtest::Join(t);
    }
};
}  // namespace

TEST(native_colour_foreign_publish_is_deferred_to_the_game_thread) {
    // Review #5: only the thread that ran CMBUILD writes into the manager.
    CmFixture f;

    CHECK(f.published != 0);
    CmFixture::OnOtherThread([] { rsxrcolour::PublishCurrent(); });  // the overlay on a foreign thread
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rsxrcolour::PublishPending());
    CmFixture::OnOtherThread([] { rsxrcolour::DrainPending(); });  // a drain elsewhere does nothing
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rsxrcolour::PublishPending());
    rsxrcolour::DrainPending();  // the game thread (FGHK / SetTexture)
    CHECK_EQ(CmFixture::Slot(), f.published);
    CHECK(!rsxrcolour::PublishPending());
}

TEST(native_colour_foreign_palette_waits_for_a_complete_game_thread_snapshot) {
    CmFixture f;
    rsxrtest::Restore restoreNormal(rsxrcolour::g_colour.fallback7[0]);
    rsxrtest::Restore restoreCb(rsxrcolour::g_colour.fallback7[1]);
    const uint32_t beforeFallback = rsxrcolour::g_colour.fallback7[0];
    float before[6];

    std::memcpy(before, g_rsxr_fx_rgb, sizeof before);
    CmFixture::OnOtherThread([] {
        const uint32_t rgb[2] = {0x123456, 0xABCDEF};

        rsxrcolour::SetPalette(rgb);
        rsxrcolour::PublishCurrent();
    });
    CHECK(std::memcmp(before, g_rsxr_fx_rgb, sizeof before) == 0);
    CHECK_EQ(rsxrcolour::g_colour.fallback7[0], beforeFallback);
    CHECK(rsxrcolour::PublishPending());
    rsxrcolour::DrainPending();
    CHECK_EQ(g_rsxr_fx_rgb[0], 0x12 / 255.0f);
    CHECK_EQ(g_rsxr_fx_rgb[2], 0x56 / 255.0f);
    CHECK_EQ(g_rsxr_fx_rgb[3], 0xAB / 255.0f);
    CHECK_EQ(g_rsxr_fx_rgb[5], 0xEF / 255.0f);
    CHECK_EQ(rsxrcolour::g_colour.fallback7[0], 0x123456u);
    CHECK_EQ(rsxrcolour::g_colour.fallback7[1], 0xABCDEFu);
    std::memcpy(g_rsxr_fx_rgb, before, sizeof before);
}

TEST(native_colour_faulted_publication_retries_and_quarantines_until_rebuild) {
    CmFixture f;
    auto& ptr = *reinterpret_cast<float**>(CmFixture::mgr + rsxrcolour::PtrOffset(0, 1, 0));

    ptr = reinterpret_cast<float*>(1);
    rsxrcolour::PublishCurrent();
    CHECK(rsxrcolour::PublishPending());
    ptr = nullptr;
    rsxrcolour::DrainPending();
    CHECK(!rsxrcolour::PublishPending());
    CHECK_EQ(CmFixture::Slot(), f.published);
    ptr = reinterpret_cast<float*>(1);
    rsxrcolour::PublishCurrent();
    rsxrcolour::DrainPending();
    rsxrcolour::DrainPending();
    ptr = nullptr;
    CmFixture::Slot() = 0;
    rsxrcolour::DrainPending();
    CHECK(rsxrcolour::PublishPending());
    CHECK_EQ(CmFixture::Slot(), 0u);
    RsxrMid::Context c{};

    c.esi = uint32_t(uintptr_t(CmFixture::mgr));
    RsxrColour_CMBUILD(c);
    CHECK(!rsxrcolour::PublishPending());
    CHECK_EQ(CmFixture::Slot(), f.published);
}

TEST(ramp_v6_store_never_touches_the_colour_manager) {
    // Review #5: StoreV6 runs on the ramp worker; it may only request a publish.
    CmFixture f;

    CmFixture::OnOtherThread([] { RsxrCand_ResetForTests(); });  // StoreV6(false) on a foreign thread
    CHECK_EQ(CmFixture::Slot(), 0u);
    CHECK(rsxrcolour::PublishPending());
    RsxrCand_ResetForTests();  // on the game thread too: still only a request
    CHECK_EQ(CmFixture::Slot(), 0u);
    rsxrcolour::DrainPending();
    CHECK_EQ(CmFixture::Slot(), f.published);
}

TEST(native_colour_cmbuild_drains_a_request_made_before_the_build) {
    CmFixture f;

    rsxrcolour::ResetThreadForTests();  // a new song: no manager thread yet
    rsxrcolour::RequestPublish();
    rsxrcolour::DrainPending();  // no CMBUILD thread known: stays pending
    CHECK_EQ(CmFixture::Slot(), 0u);
    RsxrMid::Context c = {};

    c.esi = uint32_t(uintptr_t(CmFixture::mgr));
    RsxrColour_CMBUILD(c);
    CHECK_EQ(CmFixture::Slot(), f.published);
    CHECK(!rsxrcolour::PublishPending());
}

// RSMods GetStringColor uses mode*0xA8 and the stock state offsets, including in CB mode.
TEST(native_colour_rsmods_six_column_abi_shares_the_correct_rgb_values) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 1;
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    float stock[2][28][6][3] = {};

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                stock[m][st][s][0] = float(m * 168 + st * 6 + s);
                *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, st, s)) = float(m * 168 + st * 6 + s);
                *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(m, st, s)) = stock[m][st][s];
            }
        }
    }

    rsxrcolour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                auto* legacy = *reinterpret_cast<float**>(mgr + 0x350 + 4 * (m * 0xA8 + st * 6 + s));

                CHECK(legacy == rsxrcolour::GetRGB(mgr, m, st, s));
                CHECK_EQ(*reinterpret_cast<float*>(mgr + 0x890 + 4 * (m * 0xA8 + st * 6 + s)),
                         float(m * 168 + st * 6 + s));

                if (legacy && legacy == stock[m][st][s]) {
                    legacy[1] = 0.25f;  // custom/rainbow write through RSMods' address
                    const float* rgb = rsxrcolour::GetRGB(mgr, m, st, s);

                    CHECK(rgb && rgb[1] == 0.25f);
                }
            }
        }
    }
}

extern "C" void __cdecl Rsxr_CallIn_GlowColour(RsxrMid::Context& c);

TEST(native_colour_glow_callin_reads_all_eight_slots_in_both_modes) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 1;
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    float colours[2][8][3] = {};

    for (uint32_t mode = 0; mode < 2; ++mode) {
        for (uint32_t slot = 0; slot < 8; ++slot) {
            *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(mode, 3, slot)) = colours[mode][slot];
            RsxrMid::Context c{};

            c.ecx = uint32_t(reinterpret_cast<uintptr_t>(mgr));
            c.eax = mode * 0xA8 + slot;
            c.edx = slot;
            Rsxr_CallIn_GlowColour(c);
            const auto* rgbPtr = reinterpret_cast<float*>(uintptr_t(c.eax));

            CHECK(rgbPtr == colours[mode][slot]);
        }
    }
}

TEST(native_colour_extended_bass_low_b_takes_string_6_colours_and_restores) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 1;
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    float stock[2][28][6][3] = {};

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            for (int s = 0; s < 6; ++s) {
                *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(m, st, s)) = stock[m][st][s];
                *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, st, s)) = float(s) / 16;
            }
        }
    }

    rsxrcolour::SetBassLowB(true);
    rsxrcolour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            CHECK(rsxrcolour::GetRGB(mgr, m, st, 4) == rsxrcolour::GetRGB(mgr, m, st, 6));
            // The six-column table RSMods reads keeps the stock slot 4.
            CHECK(*reinterpret_cast<float**>(mgr + 0x350 + 4 * ((m * 28 + st) * 6 + 4)) == stock[m][st][4]);
        }

        CHECK(*reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, 15, 4)) ==
              *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, 15, 6)));
    }

    rsxrcolour::Publish(mgr);  // a second publish swaps from the stock values, not from the swapped ones
    CHECK(rsxrcolour::GetRGB(mgr, 0, 1, 4) == rsxrcolour::GetRGB(mgr, 0, 1, 6));

    rsxrcolour::SetBassLowB(false);
    rsxrcolour::Publish(mgr);

    for (int m = 0; m < 2; ++m) {
        for (int st = 0; st < 28; ++st) {
            CHECK(rsxrcolour::GetRGB(mgr, m, st, 4) == stock[m][st][4]);
            CHECK(*reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(m, st, 4)) == 4.0f / 16);
        }
    }
}

// CMINIT swaps slot 4, then the game's table build rewrites slot 4 before CMBUILD publishes again: the build's entries
// stay, they are not overwritten with the ones saved at CMINIT.
TEST(native_colour_extended_bass_low_b_keeps_slot_4_the_build_rewrote) {
    rsxrtest::Restore restore_g_rsxr_native_colours(g_rsxr_native_colours);

    g_rsxr_native_colours = 1;
    alignas(4) uint8_t mgr[rsxrcolour::kSize] = {};
    float ctor[3] = {}, built[3] = {}, six[3] = {};

    *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(0, 1, 4)) = ctor;
    *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(0, 1, 6)) = six;
    *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(0, 15, 4)) = 0.25f;
    rsxrcolour::SetBassLowB(true);
    rsxrcolour::Publish(mgr);  // CMINIT
    CHECK(rsxrcolour::GetRGB(mgr, 0, 1, 4) == rsxrcolour::GetRGB(mgr, 0, 1, 6));

    *reinterpret_cast<float**>(mgr + rsxrcolour::PtrOffset(0, 1, 4)) = built;  // the build
    *reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(0, 15, 4)) = 0.75f;
    rsxrcolour::Publish(mgr);  // CMBUILD
    CHECK(rsxrcolour::GetRGB(mgr, 0, 1, 4) == rsxrcolour::GetRGB(mgr, 0, 1, 6));

    rsxrcolour::SetBassLowB(false);
    rsxrcolour::Publish(mgr);
    CHECK(rsxrcolour::GetRGB(mgr, 0, 1, 4) == built);
    CHECK(*reinterpret_cast<float*>(mgr + rsxrcolour::UVOffset(0, 15, 4)) == 0.75f);
}

TEST(colours_bass_low_b_takes_slot_6_only_for_a_shifted_player) {
    CHECK(rsxrcolour::BassSlot(4, true) == 6u);
    CHECK(rsxrcolour::BassSlot(4, false) == 4u);

    for (uint32_t s : {0u, 1u, 2u, 3u, 5u, 6u, 7u}) {
        CHECK(rsxrcolour::BassSlot(s, true) == s);
    }

    RsxrMid::Context c{};

    rsxrcolour::SetBassShift(1, true);
    c.eax = 1;
    c.ecx = 0xABCD0004u;
    Rsxr_Mid_CBUV(c);
    CHECK(c.ecx == 0xABCD0006u);  // only CL changes
    c.eax = 0;
    c.ecx = 4;
    Rsxr_Mid_CBSTR(c);
    CHECK(c.ecx == 4u);  // player 0 (a guitar) keeps slot 4

    uint32_t stack[3] = {0, 0, 4};

    c.esp = uint32_t(reinterpret_cast<uintptr_t>(stack));
    c.ecx = 1;
    Rsxr_Mid_CBRGB(c);
    CHECK(stack[2] == 6u);
    rsxrcolour::SetBassShift(1, false);
    stack[2] = 4;
    Rsxr_Mid_CBRGB(c);
    CHECK(stack[2] == 4u);
}
