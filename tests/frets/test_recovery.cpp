#include "executable.h"
#include "features/frets/frets.h"
#include "features/string7/string7.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cmath>
#include <cstring>
#include <vector>

extern "C" void __cdecl Rs7_Mid_FRG1(Rs7Mid::Context& c);

namespace {
void __cdecl SetPos(uint8_t* entity, const float* p) {
    std::memcpy(entity, p, 12);
}

struct Fixture {
    rs7test::Restore<uintptr_t> on{g_rs7_frets_on}, pos{g_rs7_fn_setpos}, scale{g_rs7_fn_setscale};
    rs7test::Executable scaleCode{0xC3};  // ret (Entity_SetScale: EAX entity, cdecl float)
    alignas(4) uint8_t guitar[0x40] = {}, info[0x10] = {}, gs[0x10] = {}, o0[0x20] = {}, o1[0x20] = {};
    uint8_t entity[12] = {};
    std::vector<uint8_t> grid = std::vector<uint8_t>(rs7frets::kBars + 2 * rs7frets::kBarStride);

    Fixture() {
        g_rs7_frets_on = 1;
        g_rs7_fn_setpos = uintptr_t(&SetPos);
        g_rs7_fn_setscale = uintptr_t(scaleCode.p);
        info[rs7::kInfoKind] = 1;
        info[rs7::kInfoStrings] = 7;
        info[rs7::kInfoFretBars] = 1;
        Put(guitar, rs7::kGuitarInfo, info);
        Put(guitar, rs7::kGuitarStrings, gs);
        Put(guitar, rs7::kGuitarGrid, grid.data());
        Put(grid.data(), 0, guitar);
        Put(gs, 4, o0);
        Put(gs, 8, o1);
        Put(o1, 0x08, 1.0f);
        Put(o1, 0x14, 1.0f);
        Bar()[0] = 1;
        Put(Bar(), rs7frets::kBarEntity, entity);
        Put(Bar(), rs7frets::kBarALocal, 6.0f);
        Put(Bar(), rs7frets::kBarAWorld, 6.0f);
        Put(Bar(), rs7frets::kBarBLocal, -1.0f);
        ResetWorld();
    }

    ~Fixture() { Rs7Frets_Forget(guitar); }

    template <class T>
    static void Put(uint8_t* p, uint32_t off, T v) {
        std::memcpy(p + off, &v, sizeof v);
    }

    uint8_t* Bar(int f = 0) { return grid.data() + rs7frets::kBars + f * rs7frets::kBarStride; }

    void ResetWorld() {
        for (int f = 0; f < 2; ++f) {
            Put(Bar(f), rs7frets::kBarBWorld, -1.0f);
        }
    }

    void CheckFloor(float x) {
        for (int f = 0; f < 2; ++f) {
            CHECK_EQ(*reinterpret_cast<float*>(Bar(f) + rs7frets::kBarBWorld), x);
        }
    }
};
}  // namespace

TEST(frets_f2_recovers_a_missed_f1_floor_without_accumulating) {
    Fixture f;

    Fixture::Put<uint8_t*>(f.gs, 4, nullptr);
    Rs7_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    Fixture::Put(f.gs, 4, f.o0);
    rs7frets::FrameFit fit{};

    CHECK(Rs7Frets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 1.0f);  // no successful floor movement yet
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    CHECK(Rs7Frets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 2.0f);
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    f.ResetWorld();  // the next stock transform replaces the world ends
    Rs7_FretsAfterApply(f.grid.data());
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_f2_retries_the_floor_even_after_the_bars_were_applied) {
    Fixture f;

    f.Bar()[0] = 0;
    Rs7_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    f.Bar()[0] = 1;
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_f2_recovers_when_equal_attach_vectors_become_valid) {
    Fixture f;

    Fixture::Put(f.o1, 0x08, 0.0f);
    Rs7_FretsAfterApply(f.grid.data());
    f.CheckFloor(-1.0f);
    Fixture::Put(f.o1, 0x08, 1.0f);
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
    CHECK(std::fabs(*reinterpret_cast<float*>(f.entity) - 2.0f) < 1e-4f);
    rs7frets::FrameFit fit{};

    CHECK(Rs7Frets_ChordFrame(f.guitar, 6, &fit));
    CHECK_EQ(fit.origin, 2.0f);
    Rs7Frets_Frame(f.guitar, true);
    f.CheckFloor(-2.0f);
}

TEST(frets_six_string_bass_glow_extends_the_end_opposite_the_floor) {
    for (const bool inverted : {false, true}) {
        Fixture f;

        f.info[rs7::kInfoKind] = 2;
        f.info[rs7::kInfoStrings] = 6;
        Fixture::Put(f.Bar(), rs7frets::kBarBLocal, inverted ? -1.0f : 4.0f);
        Fixture::Put(f.Bar(), rs7frets::kBarBWorld, inverted ? -1.0f : 4.0f);
        Rs7_FretsAfterApply(f.grid.data());
        std::vector<uint8_t> player(rs7::kSessionGuitar + 4);
        alignas(4) uint8_t fx[0x80] = {};
        uint32_t frame[4] = {};

        Fixture::Put(player.data(), rs7::kSessionGuitar, f.guitar);
        frame[3] = uintptr_t(player.data());

        for (const uint32_t off : {0x10u, 0x28u}) {
            Fixture::Put(fx, off, inverted ? -2.0f : -1.0f);
            Fixture::Put(fx, off + 12, inverted ? 4.0f : 5.0f);
        }

        Rs7Mid::Context c{};

        c.ebx = uintptr_t(fx);
        c.ebp = uintptr_t(frame);
        Rs7_Mid_FRG1(c);

        for (const uint32_t off : {0x10u, 0x28u}) {
            CHECK_EQ(*reinterpret_cast<float*>(fx + off), -2.0f);
            CHECK_EQ(*reinterpret_cast<float*>(fx + off + 12), 5.0f);
        }
    }
}
