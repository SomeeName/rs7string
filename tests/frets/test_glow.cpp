#include "features/frets/frets.h"
#include "platform/midhook.h"
#include "test.h"
#include "testutil.h"
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" void __cdecl Rsxr_Mid_FRG1(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_FRG2(RsxrMid::Context& c);
extern "C" void __cdecl Rsxr_Mid_FRG3(RsxrMid::Context& c);

// FRG1 / FRG2 (features/frets/hooks.cpp): the fret range glow's bars are queued by FRG1 and scaled by FRG2 to span
// all strings. With inverted strings the floor (the wires' b ends) lies on the extra strings' side; FRG1 then leaves
// the wire ends alone (ShiftFloor moved them) but must still queue the bars.
namespace {
uint8_t* g_scaledEntity[8] = {};
float g_scaledK[8] = {};
int g_scaleCalls = 0;

void __cdecl RecordScale(uint8_t* entity, float k) {
    if (g_scaleCalls < 8) {
        g_scaledEntity[g_scaleCalls] = entity;
        g_scaledK[g_scaleCalls] = k;
    }

    ++g_scaleCalls;
}

// Entity_SetScale's convention (EAX = entity, cdecl float k), built as machine code: push [esp+4]; push eax;
// call RecordScale; add esp, 8; ret.
uintptr_t FakeSetScale() {
    static uint8_t* code = [] {
        auto* b = rsxrtest::Must(
            static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)),
            "VirtualAlloc");
        const uint8_t head[] = {0xFF, 0x74, 0x24, 0x04, 0x50, 0xE8};

        std::memcpy(b, head, sizeof head);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(&RecordScale) - reinterpret_cast<uintptr_t>(b + 10));

        std::memcpy(b + 6, &rel, 4);
        const uint8_t tail[] = {0x83, 0xC4, 0x08, 0xC3};

        std::memcpy(b + 10, tail, sizeof tail);

        return b;
    }();

    return reinterpret_cast<uintptr_t>(code);
}

template <class T>
void Put(uint8_t* b, uint32_t off, T v) {
    std::memcpy(b + off, &v, sizeof(T));
}

void PutVec(uint8_t* b, uint32_t off, float x, float y, float z) {
    const float v[3] = {x, y, z};

    std::memcpy(b + off, v, sizeof(v));
}

uint32_t Addr(const void* p) {
    return uint32_t(reinterpret_cast<uintptr_t>(p));
}
}  // namespace

TEST(frets_glow_bars_are_queued_when_the_floor_is_already_shifted) {
    rsxrtest::Restore restore_g_rsxr_fn_setscale(g_rsxr_fn_setscale);

    g_rsxr_fn_setscale = FakeSetScale();
    g_scaleCalls = 0;
    RsxrFrets_ResetLog();  // no glow bars queued from earlier tests

    alignas(4) uint8_t info[0x10] = {};

    info[rsxr::kInfoKind] = 1;     // guitar
    info[rsxr::kInfoStrings] = 7;  // built with 7 strings: one extra lane
    info[rsxr::kInfoFretBars] = 25;
    alignas(4) uint8_t o0[0x20] = {}, o1[0x20] = {}, gs[0x10] = {};

    PutVec(o0, 0x08, 0, 0, 0);  // string 0 attach (a0) and wire end (w0)
    PutVec(o0, 0x14, 0, 0, 0);
    PutVec(o1, 0x08, 2, 0, 0);  // string 1: spacing 2
    PutVec(o1, 0x14, 2, 0, 0);
    Put<uint8_t*>(gs, 4, o0);
    Put<uint8_t*>(gs, 8, o1);
    std::vector<uint8_t> grid(rsxrfrets::kBars + 26 * rsxrfrets::kBarStride, 0);
    uint8_t* bar0 = grid.data() + rsxrfrets::kBars;

    bar0[0] = 1;
    PutVec(bar0, rsxrfrets::kBarBLocal, -10, 0, 0);  // inverted: the floor beyond string 0, on the extra string's side
    alignas(4) uint8_t guitar[0x40] = {};

    Put<uint8_t*>(guitar, rsxr::kGuitarStrings, gs);
    Put<uint8_t*>(guitar, rsxr::kGuitarGrid, grid.data());
    Put<uint8_t*>(guitar, rsxr::kGuitarInfo, info);
    Put<uint8_t*>(grid.data(), 0, guitar);
    Rsxr_FretsAfterApply(grid.data());
    std::vector<uint8_t> player(rsxr::kSessionGuitar + 4, 0);

    Put<uint8_t*>(player.data(), rsxr::kSessionGuitar, guitar);
    alignas(4) uint8_t entities[5][4] = {};
    alignas(4) uint8_t objs[4][0x14] = {};
    alignas(4) uint8_t box[0x14] = {};
    alignas(4) uint8_t fx[0x80] = {};

    Put<uint32_t>(box, 0x0C, 0xFA400094u);  // the glow's box
    Put<uint8_t*>(box, 0x10, entities[4]);
    Put<uint8_t*>(fx, 0x6C, box);

    for (int i = 0; i < 4; ++i) {
        Put<uint32_t>(objs[i], 0x0C, (i % 2) != 0 ? 0x68058637u : 0xAF25DCE0u);  // the glow's two bar effects
        Put<uint8_t*>(objs[i], 0x10, entities[i]);
        Put<uint8_t*>(fx, 0x70 + 4 * uint32_t(i), objs[i]);
    }

    for (const uint32_t off : {0x10u, 0x1Cu, 0x28u, 0x34u}) {
        PutVec(fx, off, -1, 0, 0);  // past string 0: GlowEnd would move it
    }

    uint32_t frame[4] = {};  // [ebp+0Ch] = the highway (player)

    frame[3] = Addr(player.data());
    RsxrMid::Context c{};

    c.ebx = Addr(fx);
    c.ebp = Addr(frame);
    Rsxr_Mid_FRG1(c);

    // Whatever ShiftFloor did to them, FRG1 puts the floor-side wire ends exactly on the floor (#283): the stock margin
    // (bar B at -10 = 5 spacings past string 0) plus the one extra lane = 6 spacings of 2.
    for (const uint32_t off : {0x10u, 0x1Cu, 0x28u, 0x34u}) {
        float v[3];

        std::memcpy(v, fx + off, sizeof(v));
        CHECK(std::fabs(v[0] + 12.0f) < 1e-3f && v[1] == 0.0f && v[2] == 0.0f);
    }

    // FRG2 scales each queued bar and FRG3 the box: (stock far 5 + 1 lane) * spacing 2 plus the stock margin (5
    // spacings) on both sides, over the stock strings' width plus that margin (#283: the box kept its stock height).
    const float k = (6.0f * 2.0f + 20.0f) / (rsxrfrets::kStockStringsWidth + 20.0f);

    for (int i = 0; i < 4; ++i) {
        RsxrMid::Context c2{};

        c2.ebx = Addr(objs[i]);
        Rsxr_Mid_FRG2(c2);
    }

    RsxrMid::Context c3{};

    c3.edi = Addr(box);
    Rsxr_Mid_FRG3(c3);
    CHECK_EQ(g_scaleCalls, 5);

    for (int i = 0; i < 5 && i < g_scaleCalls; ++i) {
        CHECK(g_scaledEntity[i] == entities[i]);
        CHECK(std::fabs(g_scaledK[i] - k) < 1e-4f);
    }

    RsxrFrets_ResetLog();
    RsxrFrets_Forget(guitar);
}
