// PL1 (chord pluck of string 6, features/nslot/hooks_eval.cpp): the hook leaves every register as it came, so the
// replayed `mov al,[ebp-10h]` keeps EAX's upper bytes like the stock code.
#include "platform/midhook.h"
#include "test.h"
#include <cstdint>

extern "C" void __cdecl Rs7_Mid_PL1(Rs7Mid::Context&);

namespace {
uint32_t Addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// One PL1 hit on a T8 whose string-6 fret is `fret`: EDI = T8 + 0Ah, [EBP+8] = a session whose GuitarStrings
// (session+37Ch -> +0Ch) is null, so the pluck helper returns without touching the game.
Rs7Mid::Context Hit(uint8_t fret) {
    static uint8_t t8[0x80];
    static uint8_t guitar[0x10];
    static uint8_t session[0x380];
    static uint32_t frame[4];

    t8[0x0A + 0x44] = fret;
    *reinterpret_cast<uint32_t*>(session + 0x37C) = Addr(guitar);
    frame[2] = Addr(session);
    Rs7Mid::Context c{};

    c.edi = Addr(t8 + 0x0A);
    c.ebp = Addr(frame);
    c.eax = 0xA5A5A507;
    c.jump = 0x1234;
    Rs7_Mid_PL1(c);

    return c;
}
}  // namespace

TEST(nslot_pl1_keeps_eax_and_the_replay) {
    for (const uint8_t fret : {uint8_t(0xFF), uint8_t(0), uint8_t(5)}) {
        const Rs7Mid::Context c = Hit(fret);

        CHECK(c.eax == 0xA5A5A507);
        CHECK(c.jump == 0x1234);
    }
}
