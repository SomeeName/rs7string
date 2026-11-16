#include "features/frets/frets.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <cstring>
#include <vector>

extern "C" void __cdecl Rs7_Mid_SC1(Rs7Mid::Context& c);

namespace {
struct Camera {
    rs7test::Restore<uintptr_t> on{g_rs7_frets_on};
    alignas(4) uint8_t guitar[0x40] = {}, info[0x10] = {}, strings[12] = {}, o0[0x20] = {}, o1[0x20] = {};
    std::vector<uint8_t> grid = std::vector<uint8_t>(rs7frets::kBars + rs7frets::kBarStride);
    float stack[16] = {};

    template <class T>
    static void Put(uint8_t* p, uint32_t off, T value) {
        std::memcpy(p + off, &value, sizeof value);
    }

    Camera(int kind, int count, bool inverted, bool lefty) {
        g_rs7_frets_on = 1;
        info[rs7::kInfoKind] = uint8_t(kind);
        info[rs7::kInfoStrings] = uint8_t(count);
        info[8] = uint8_t(inverted);
        info[rs7::kInfoLefty] = uint8_t(lefty);
        Put(guitar, rs7::kGuitarInfo, info);
        Put(guitar, rs7::kGuitarStrings, strings);
        Put(guitar, rs7::kGuitarGrid, grid.data());
        Put(strings, 4, o0);
        Put(strings, 8, o1);
        Put(o1, 0x08, lefty ? -0.25f : 0.25f);
        Put(o1, 0x10, inverted ? 2.0f : -2.0f);
        uint8_t* bar = grid.data() + rs7frets::kBars;

        bar[0] = 1;
        Put(bar, rs7frets::kBarBLocal + 8, -20.0f);
    }

    void Check(float shift) {
        // Each send rebuilds the outgoing values. Neither the attach points nor the guitar position may change.
        const std::vector<uint8_t> before(guitar, guitar + sizeof guitar);
        const std::vector<uint8_t> beforeGrid = grid;
        const std::vector<uint8_t> before0(o0, o0 + sizeof o0), before1(o1, o1 + sizeof o1);

        for (int repeat = 0; repeat < 2; ++repeat) {
            for (int i = 0; i < 16; ++i) {
                stack[i] = float(i);
            }

            Rs7Mid::Context c{};

            c.esi = uintptr_t(guitar);
            c.esp = uintptr_t(stack);
            const float z = 100.0f;

            std::memcpy(&c.edx, &z, sizeof z);
            const Rs7Mid::Context beforeContext = c;

            Rs7_Mid_SC1(c);
            float result;

            std::memcpy(&result, &c.edx, sizeof result);
            CHECK_EQ(result, z + shift);
            Rs7Mid::Context afterContext = c;

            afterContext.edx = beforeContext.edx;
            CHECK(std::memcmp(&afterContext, &beforeContext, sizeof c) == 0);
            CHECK_EQ(c.esi, uintptr_t(guitar));
            CHECK_EQ(c.esp, uintptr_t(stack));
            CHECK_EQ(c.jump, 0u);  // replay the displaced loads

            for (int i = 0; i < 16; ++i) {
                const bool moved = i == 6 || i == 9 || i == 12;

                CHECK_EQ(stack[i], float(i) + (moved ? shift : 0.0f));
            }

            CHECK(std::memcmp(guitar, before.data(), sizeof guitar) == 0);
            CHECK(grid == beforeGrid);
            CHECK(std::memcmp(o0, before0.data(), sizeof o0) == 0);
            CHECK(std::memcmp(o1, before1.data(), sizeof o1) == 0);
        }
    }
};
}  // namespace

TEST(song_camera_follows_the_floor_for_each_instrument_and_orientation) {
    for (const bool lefty : {false, true}) {
        for (const bool inverted : {false, true}) {
            for (const int count : {6, 7, 8}) {
                Camera c(1, count, inverted, lefty);

                c.Check(inverted && count > 6 ? -2.0f * float(count - 6) : 0.0f);
            }

            for (const int count : {4, 5, 6}) {
                Camera c(2, count, inverted, lefty);

                c.Check(count == 6 || (inverted && count == 5) ? -2.0f : 0.0f);
            }
        }
    }
}

TEST(song_camera_waits_for_floor_geometry_and_respects_the_frets_gate) {
    Camera c(1, 8, true, false);

    g_rs7_frets_on = 0;
    c.Check(0.0f);
    g_rs7_frets_on = 1;
    Camera::Put<uint8_t*>(c.strings, 4, nullptr);
    c.Check(0.0f);
    Camera::Put(c.strings, 4, c.o0);
    c.grid[rs7frets::kBars] = 0;
    c.Check(0.0f);
    c.grid[rs7frets::kBars] = 1;
    c.Check(-4.0f);  // ready again: retry without a cached or guessed spacing
    Camera::Put(c.o1, 0x08, 0.0f);
    Camera::Put(c.o1, 0x10, 0.0f);
    c.Check(0.0f);
    Camera::Put<uint8_t*>(c.guitar, rs7::kGuitarGrid, nullptr);
    c.Check(0.0f);
}
