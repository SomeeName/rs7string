#include "executable.h"
#include "features/frets/frets.h"
#include "features/string7/string7.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cmath>
#include <cstring>
#include <vector>

extern "C" void __cdecl Rsxr_Mid_FRG1(RsxrMid::Context& c);

namespace {
void __cdecl SetPos(uint8_t* entity, const float* p) {
    std::memcpy(entity, p, 12);
}

struct Fixture {
    rsxrtest::Restore<uintptr_t> on{g_rsxr_frets_on}, pos{g_rsxr_fn_setpos}, scale{g_rsxr_fn_setscale};
    rsxrtest::Executable scaleCode{0xC3};  // ret (Entity_SetScale: EAX entity, cdecl float)
    alignas(4) uint8_t guitar[0x40] = {}, info[0x10] = {}, gs[0x10] = {}, o0[0x20] = {}, o1[0x20] = {};
    uint8_t entity[12] = {};
    std::vector<uint8_t> grid = std::vector<uint8_t>(rsxrfrets::kBars + 2 * rsxrfrets::kBarStride);

    Fixture() {
        g_rsxr_frets_on = 1;
        g_rsxr_fn_setpos = uintptr_t(&SetPos);
        g_rsxr_fn_setscale = uintptr_t(scaleCode.p);
        info[rsxr::kInfoKind] = 1;
        info[rsxr::kInfoStrings] = 7;
        info[rsxr::kInfoFretBars] = 1;
        Put(guitar, rsxr::kGuitarInfo, info);
        Put(guitar, rsxr::kGuitarStrings, gs);
        Put(guitar, rsxr::kGuitarGrid, grid.data());
        Put(grid.data(), 0, guitar);
        Put(gs, 4, o0);
        Put(gs, 8, o1);
        Put(o1, 0x08, 1.0f);
        Put(o1, 0x14, 1.0f);
        Bar()[0] = 1;
        Put(Bar(), rsxrfrets::kBarEntity, entity);
        Put(Bar(), rsxrfrets::kBarALocal, 6.0f);
        Put(Bar(), rsxrfrets::kBarAWorld, 6.0f);
        Put(Bar(), rsxrfrets::kBarBLocal, -1.0f);
        ResetWorld();
    }

    ~Fixture() { RsxrFrets_Forget(guitar); }

    template <class T>
    static void Put(uint8_t* p, uint32_t off, T v) {
        std::memcpy(p + off, &v, sizeof v);
    }

    uint8_t* Bar(int f = 0) { return grid.data() + rsxrfrets::kBars + f * rsxrfrets::kBarStride; }

    void ResetWorld() {
        for (int f = 0; f < 2; ++f) {
            Put(Bar(f), rsxrfrets::kBarBWorld, -1.0f);
        }
    }

    void CheckFloor(float x) {
        for (int f = 0; f < 2; ++f) {
            CHECK_EQ(*reinterpret_cast<float*>(Bar(f) + rsxrfrets::kBarBWorld), x);
        }
    }
};
}  // namespace

TEST(frets_f2_recovers_a_missed_f1_floor_without_accumulating) {
    Fixture f;

    Fixture::Put<uint8_t*>(f.gs, 4, nullptr);
    Rsxr_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    Fixture::Put(f.gs, 4, f.o0);
    rsxrfrets::FrameFit fit{};

    CHECK(RsxrFrets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 1.0f);  // no successful floor movement yet
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    CHECK(RsxrFrets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 2.0f);
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    f.ResetWorld();  // the next stock transform replaces the world ends
    Rsxr_FretsAfterApply(f.grid.data());
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_f2_retries_the_floor_even_after_the_bars_were_applied) {
    Fixture f;

    f.Bar()[0] = 0;
    Rsxr_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    f.Bar()[0] = 1;
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_f2_recovers_when_equal_attach_vectors_become_valid) {
    Fixture f;

    Fixture::Put(f.o1, 0x08, 0.0f);
    Rsxr_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    Fixture::Put(f.o1, 0x08, 1.0f);
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    rsxrfrets::FrameFit fit{};

    CHECK(RsxrFrets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 2.0f);
    RsxrFrets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_six_string_bass_glow_extends_the_end_opposite_the_floor) {
    for (const bool inverted : {false, true}) {
        Fixture f;

        f.info[rsxr::kInfoKind] = 2;
        f.info[rsxr::kInfoStrings] = 6;
        Fixture::Put(f.Bar(), rsxrfrets::kBarBLocal, inverted ? -1.0f : 4.0f);
        Fixture::Put(f.Bar(), rsxrfrets::kBarBWorld, inverted ? -1.0f : 4.0f);
        Rsxr_FretsAfterApply(f.grid.data());
        std::vector<uint8_t> player(rsxr::kSessionGuitar + 4);
        alignas(4) uint8_t fx[0x80] = {};
        uint32_t frame[4] = {};

        Fixture::Put(player.data(), rsxr::kSessionGuitar, f.guitar);
        frame[3] = uintptr_t(player.data());

        for (const uint32_t off : {0x10u, 0x28u}) {
            Fixture::Put(fx, off, inverted ? -2.0f : -1.0f);
            Fixture::Put(fx, off + 12, inverted ? 4.0f : 5.0f);
        }

        RsxrMid::Context c{};

        c.ebx = uintptr_t(fx);
        c.ebp = uintptr_t(frame);
        Rsxr_Mid_FRG1(c);

        for (const uint32_t off : {0x10u, 0x28u}) {
            CHECK_EQ(*reinterpret_cast<float*>(fx + off), -2.0f);
            CHECK_EQ(*reinterpret_cast<float*>(fx + off + 12), 5.0f);
        }
    }
}
