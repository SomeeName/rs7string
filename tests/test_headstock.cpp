#include "core/headstock.h"
#include "test.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace rs7head;

namespace {
// A PegSet as the stock ctor 0x59C190 leaves it in the native allocation: six slots, pegColor[6]
// at +0FCh (which lies where slots 6/7 go).
std::vector<uint8_t> StockCtorImage() {
    std::vector<uint8_t> m(kSize, 0xCD);

    *reinterpret_cast<uint32_t*>(&m[0]) = 0xFFFFFFFF;
    *reinterpret_cast<uint32_t*>(&m[4]) = 0;
    m[8] = 0;

    for (int s = 0; s < kStockSlots; ++s) {
        uint8_t* sl = &m[kSlots + s * kSlotStride];

        std::memset(sl, 0, 0x10);
        const float rgb[3] = {0.25f, 0.5f, 0.75f}, one = 1.0f;

        std::memcpy(sl + 0x10, rgb, 12);
        *reinterpret_cast<uint32_t*>(sl + 0x1C) = 1;
        std::memcpy(sl + 0x20, &one, 4);
        std::memcpy(sl + 0x24, &one, 4);
    }

    for (int i = 0; i < kStockSlots; ++i) {
        uint8_t* c = &m[kStockColours + i * kColourStride];

        c[0] = 0;

        for (int k = 0; k < 3; ++k) {
            *reinterpret_cast<uint32_t*>(c + 4 + 4 * k) = 0x3F000000u + uint32_t(i * 16 + k);
        }
    }

    return m;
}
}  // namespace

TEST(head_init_tail_gives_slots_6_7_slot_0s_defaults_and_moves_pegcolor) {
    std::vector<uint8_t> m = StockCtorImage();
    std::vector<uint8_t> before = m;

    InitTail(m.data());
    CHECK(!IsNative(m.data()));  // the layout alone is not ownership (review #1)
    CHECK_EQ(*reinterpret_cast<const uint32_t*>(&m[kMagicOff]), kMagic);  // diagnostic only
    CHECK(std::memcmp(m.data(), before.data(), kStockColours) == 0);      // header + slots 0-5 untouched

    for (int s = kStockSlots; s < kCapacity; ++s) {
        CHECK(std::memcmp(Slot(m.data(), s), Slot(m.data(), 0), kSlotStride) == 0);
    }

    for (int i = 0; i < kCapacity; ++i) {
        CHECK(std::memcmp(&m[kColours + i * kColourStride], &before[kStockColours + (i < 6 ? i : 0) * kColourStride],
                          kColourStride) == 0);
    }

    CHECK_EQ(int(m[kTail]), kCapacity);
    CHECK_EQ(int(m[kTail + 1]), 0);
    CHECK(kColours + kCapacity * kColourStride == kTail);
    CHECK(kSlots + kCapacity * kSlotStride == kColours);
}

TEST(head_not_native_without_the_tail) {
    ResetOwners();
    std::vector<uint8_t> m = StockCtorImage();

    CHECK(!IsNative(m.data()));
    CHECK(!IsNative(nullptr));
}

// ---- ownership registry (review #1) ----

extern "C" void Rs7_PegOwnedZF();  // hooks/stubs.cpp: stdcall(PegSet), ZF = registered, all GPRs kept

namespace {
struct ZfRun {
    bool zf;
    bool kept;
};

// Calls a ZF helper the way the PS* stubs do (push PegSet; call) with every GPR set to a pattern: ZF on
// return, and whether every GPR (and ESP, after the ret 4) came back unchanged.
ZfRun RunZf(void (*fn)(), const void* ps) {
    uint32_t v_eax, v_ecx, v_edx, v_ebx, v_esi, v_edi, sp_before, sp_after;
    uint8_t zflag;

    __asm {
        push ebx
        push esi
        push edi
        mov eax, ps
        push eax
        mov sp_before, esp
        mov eax, 0A1A1A1A1h
        mov ecx, 0C1C1C1C1h
        mov edx, 0D1D1D1D1h
        mov ebx, 0B1B1B1B1h
        mov esi, 05151515h
        mov edi, 0D0D0D0D0h
        call fn
        setz zflag
        mov v_eax, eax
        mov v_ecx, ecx
        mov v_edx, edx
        mov v_ebx, ebx
        mov v_esi, esi
        mov v_edi, edi
        mov sp_after, esp
        pop edi
        pop esi
        pop ebx
    }

    const bool kept = v_eax == 0xA1A1A1A1u && v_ecx == 0xC1C1C1C1u && v_edx == 0xD1D1D1D1u && v_ebx == 0xB1B1B1B1u &&
                      v_esi == 0x05151515u && v_edi == 0xD0D0D0D0u && sp_after == sp_before + 4;

    return {zflag != 0, kept};
}

const uint8_t* Fake(uintptr_t a) {
    return reinterpret_cast<const uint8_t*>(a);
}
}  // namespace

TEST(head_owner_registry_own_disown) {
    ResetOwners();
    std::vector<uint8_t> m = StockCtorImage();

    CHECK(Own(m.data()));
    CHECK(IsNative(m.data()));
    CHECK(Own(m.data()));  // already registered: no second entry
    CHECK_EQ(OwnedCount(), 1u);
    CHECK(Disown(m.data()));
    CHECK(!IsNative(m.data()));
    CHECK(!Disown(m.data()));
    CHECK(!Own(nullptr));
    CHECK_EQ(OwnedCount(), 0u);
}

TEST(head_owner_identification_ignores_the_pegset_memory) {
    ResetOwners();
    // A stock PegSet with the native tail bytes beyond it (stale or coincidental): not native.
    std::vector<uint8_t> m = StockCtorImage();

    m[kTail] = uint8_t(kCapacity);
    *reinterpret_cast<uint32_t*>(&m[kMagicOff]) = kMagic;
    CHECK(!IsNative(m.data()));
    // Unreadable addresses: decided without a read (an access would fault here).
    CHECK(!IsNative(Fake(0x10)));
    CHECK(Own(Fake(0x20)));
    CHECK(IsNative(Fake(0x20)));
    CHECK(Disown(Fake(0x20)));
    ResetOwners();
}

TEST(head_owner_ctor_registers_then_converts) {
    ResetOwners();
    std::vector<uint8_t> m = StockCtorImage();

    Rs7_PegSetCtor(m.data());
    CHECK(IsNative(m.data()));
    CHECK_EQ(int(m[kTail]), kCapacity);
    ResetOwners();
}

TEST(head_owner_full_table_leaves_the_pegset_stock) {
    ResetOwners();

    for (unsigned i = 0; i < kOwners; ++i) {
        CHECK(Own(Fake(0x1000 + i * 0x10)));
    }

    CHECK_EQ(OwnedCount(), kOwners);
    std::vector<uint8_t> m = StockCtorImage();
    const std::vector<uint8_t> before = m;

    CHECK(!Own(m.data()));
    Rs7_PegSetCtor(m.data());
    CHECK(!IsNative(m.data()));
    CHECK(m == before);                      // stock layout kept: every stub keeps the stock counts
    CHECK(Disown(Fake(0x1000 + 7 * 0x10)));  // a free makes room again
    CHECK(Own(m.data()));
    ResetOwners();
}

TEST(head_owner_stub_helper_and_disown_decide_by_the_table) {
    ResetOwners();
    std::vector<uint8_t> m = StockCtorImage();
    const uint8_t* ps = m.data();
    ZfRun r = RunZf(Rs7_PegOwnedZF, ps);

    CHECK(!r.zf && r.kept);  // not registered: stock
    r = RunZf(Rs7_PegOwnedZF, nullptr);
    CHECK(!r.zf && r.kept);  // null never matches a free (0) entry
    r = RunZf(Rs7_PegOwnedZF, Fake(0x30));
    CHECK(!r.zf && r.kept);  // no read of the PegSet
    CHECK(Own(ps));

    for (unsigned i = 0; i < kOwners - 1; ++i) {
        CHECK(Own(Fake(0x2000 + i * 0x10)));  // first .. last entry
    }

    r = RunZf(Rs7_PegOwnedZF, ps);
    CHECK(r.zf && r.kept);
    r = RunZf(Rs7_PegOwnedZF, Fake(0x2000));
    CHECK(r.zf && r.kept);
    r = RunZf(Rs7_PegOwnedZF, Fake(0x2000 + (kOwners - 2) * 0x10));
    CHECK(r.zf && r.kept);
    CHECK(Disown(ps));  // PSA2 (mid-hook callback): native, and it leaves the table
    CHECK(!IsNative(ps));
    CHECK(!Disown(ps));
    r = RunZf(Rs7_PegOwnedZF, ps);
    CHECK(!r.zf && r.kept);
    CHECK_EQ(OwnedCount(), kOwners - 1);
    ResetOwners();
}

TEST(head_loaded_counts_keep_the_load_and_start_with_six) {
    std::vector<uint8_t> m = StockCtorImage();

    InitTail(m.data());
    *reinterpret_cast<uint16_t*>(&m[kPegCount]) = 7;
    CHECK_EQ(LoadedCounts(m.data()), 6u);
    CHECK_EQ(int(*reinterpret_cast<uint16_t*>(&m[kPegCount])), 6);
    CHECK_EQ(int(m[kTail + 1]), 7);
    *reinterpret_cast<uint16_t*>(&m[kPegCount]) = 4;  // bass
    CHECK_EQ(LoadedCounts(m.data()), 4u);
    CHECK_EQ(int(m[kTail + 1]), 4);
    *reinterpret_cast<uint16_t*>(&m[kPegCount]) = 6;  // headstock_peg=0
    CHECK_EQ(LoadedCounts(m.data()), 6u);
    CHECK_EQ(int(m[kTail + 1]), 6);
}

TEST(head_named_colour_scales_the_7th_colour_like_string_0s_named_colour) {
    std::vector<uint8_t> mgr(0x350 + 2 * 28 * 6 * 4, 0);
    const float lit[4] = {0.5f, 0.25f, 0.0f, 1.0f}, named[4] = {1.0f, 0.5f, 0.0f, 1.0f};

    for (uint32_t mode = 0; mode < 2; ++mode) {
        *reinterpret_cast<const float**>(&mgr[0x350 + ((mode * 28 + 1) * 6) * 4]) = lit;
        *reinterpret_cast<const float**>(&mgr[0x350 + ((mode * 28 + 4) * 6) * 4]) = named;
    }

    *reinterpret_cast<uint32_t*>(&mgr[0x348]) = 0;
    *reinterpret_cast<uint32_t*>(&mgr[0x34C]) = 1;
    const float fx[6] = {0.1f, 0.2f, 0.3f, 0.4f, 0.2f, 0.1f};
    float out[3];

    CHECK(NamedColour(mgr.data(), 0, fx, out));
    CHECK(out[0] == 0.2f && out[1] == 0.4f && out[2] == 0.6f);  // x2 = max(named) / max(lit)
    CHECK(NamedColour(mgr.data(), 1, fx, out));
    CHECK(out[0] == 0.8f && out[1] == 0.4f && out[2] == 0.2f);
    CHECK(!NamedColour(mgr.data(), 2, fx, out));
    CHECK(!NamedColour(nullptr, 0, fx, out));
    *reinterpret_cast<uint32_t*>(&mgr[0x348]) = 2;  // unknown colour mode: stock guard
    CHECK(!NamedColour(mgr.data(), 0, fx, out));
}

TEST(head_tuner_nodes_are_the_arrow_and_the_letter) {
    CHECK(IsTunerNode("Locator_Arrow"));
    CHECK(IsTunerNode("locator_letter"));
    CHECK(IsTunerNode("LETTER_mesh"));
    CHECK(!IsTunerNode("peg_mesh"));
    CHECK(!IsTunerNode(""));
    CHECK(!IsTunerNode(nullptr));
}

TEST(peg_load_ring_is_fifo_and_empty_until_published) {
    LoadRing q;
    LoadRec r{};

    CHECK(q.Pop(r) == LoadRing::Empty);
    q.Push({0x1000, 7, 1});
    q.Push({0x2000, 6, 0});
    CHECK(q.Pop(r) == LoadRing::Got && r.ps == 0x1000 && r.loaded == 7 && r.slot6 == 1);
    CHECK(q.Pop(r) == LoadRing::Got && r.ps == 0x2000 && r.loaded == 6 && r.slot6 == 0);
    CHECK(q.Pop(r) == LoadRing::Empty);
    CHECK(q.DroppedCount() == 0);
}

TEST(peg_load_ring_never_skips_a_reserved_unpublished_slot) {
    // A producer reserved its index but has not published the payload yet (the old code advanced the
    // consumer past the empty slot for good): the consumer waits, then reads the real record once.
    LoadRing q;
    LoadRec r{};
    const long n = q.Reserve();

    CHECK(q.Pop(r) == LoadRing::Empty);
    CHECK(q.Pop(r) == LoadRing::Empty);
    q.Publish(n, {0xB, 7, 1});
    CHECK(q.Pop(r) == LoadRing::Got && r.ps == 0xB);
    CHECK(q.Pop(r) == LoadRing::Empty);
    CHECK_EQ(q.DroppedCount(), 0L);
}

TEST(peg_load_ring_overrun_drops_oldest_and_counts_it) {
    LoadRing q;
    LoadRec r{};

    for (uintptr_t i = 1; i <= 12; ++i) {
        q.Push({i, 7, 1});  // ring holds 8: 1..4 are lost
    }

    uintptr_t first = 0, got = 0;

    while (q.Pop(r) != LoadRing::Empty) {
        if (!first) {
            first = r.ps;
        }
        ++got;
    }

    CHECK_EQ(first, uintptr_t(5));
    CHECK_EQ(got, uintptr_t(8));
    CHECK_EQ(q.DroppedCount(), 4L);
}
