#include "features/headstock/pegset.h"
#include "features/colours/colours.h"
#include "platform/spinlock.h"
#include <cctype>
#include <cstring>
#include <intrin.h>

// The PegSet logic: ownership, the native layout, the load counts, the load queue and the colours. Values only, no
// game call, so the unit tests and the emulator run it (no Win32 call and no C++ exception frame on the PSC1 / PSL2
// paths).

extern "C" __declspec(dllexport) uintptr_t g_rs7_pegsets[RS7_PEG_OWNERS] = {};

// ---- ownership ----
// The table is the only native identification: IsNative and Disown scan it, nothing reads
// the PegSet to decide. Claims: compare-exchange 0 -> PegSet (PSC1 on resource-loader jobs, possibly several at
// once). Clears: PSA2, by the one thread freeing that PegSet, before the free, so a PegSet constructed at the same
// address afterwards claims a fresh entry. 0x59C300 (dtor) and the sized free 0x58DA05 have one caller each and
// run back to back (0x58D9F4 / 0x58DA0D), so every native PegSet's free passes PSA2.
// The PSU1 / PSU2 / PSM1 stubs frozen for the mid-hook oracle (tools/tests/fixtures/midhook_9c0e44f.json) scan 64.
static_assert(rs7head::kOwners == 64, "the frozen Rs7_PegOwnedZF scans 64 entries");

bool rs7head::Own(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    if (IsNative(pegset)) {
        return true;
    }

    volatile long* t = reinterpret_cast<volatile long*>(g_rs7_pegsets);

    for (unsigned i = 0; i < kOwners; ++i) {
        if (!g_rs7_pegsets[i] && _InterlockedCompareExchange(&t[i], long(a), 0) == 0) {
            return true;
        }
    }

    return false;
}

bool rs7head::Disown(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    volatile long* t = reinterpret_cast<volatile long*>(g_rs7_pegsets);

    for (unsigned i = 0; i < kOwners; ++i) {
        if (g_rs7_pegsets[i] == a && _InterlockedCompareExchange(&t[i], 0, long(a)) == long(a)) {
            return true;
        }
    }

    return false;
}

bool rs7head::IsNative(const uint8_t* pegset) noexcept {
    if (!pegset) {
        return false;
    }

    const uintptr_t a = reinterpret_cast<uintptr_t>(pegset);
    const volatile uintptr_t* t = g_rs7_pegsets;

    for (unsigned i = 0; i < kOwners; ++i) {
        if (t[i] == a) {
            return true;
        }
    }

    return false;
}

unsigned rs7head::OwnedCount() noexcept {
    unsigned n = 0;
    const volatile uintptr_t* t = g_rs7_pegsets;

    for (unsigned i = 0; i < kOwners; ++i) {
        n += t[i] != 0;
    }

    return n;
}

void rs7head::ResetOwners() noexcept {
    for (auto& e : g_rs7_pegsets) {
        e = 0;
    }
}

// ---- the native layout and the load counts ----

void rs7head::InitTail(uint8_t* ps) {
    uint8_t colours[kStockSlots][kColourStride];

    std::memcpy(colours, ps + kStockColours, sizeof colours);  // before slots 6/7 overwrite them

    for (int s = kStockSlots; s < kCapacity; ++s) {
        std::memcpy(Slot(ps, s), Slot(ps, 0), kSlotStride);
    }

    for (int i = 0; i < kCapacity; ++i) {
        std::memcpy(ps + kColours + uint32_t(i) * kColourStride, colours[i < kStockSlots ? i : 0], kColourStride);
    }

    ps[kTail] = uint8_t(kCapacity);
    ps[kTail + 1] = 0;
    ps[kTail + 2] = 0;
    ps[kTail + 3] = 0;
    *reinterpret_cast<uint32_t*>(ps + kMagicOff) = kMagic;
}

uint32_t rs7head::LoadCount(uint32_t strings, bool native, bool headOn, uint32_t pegLoad, uint32_t modelPegs,
                            int kind) {
    if (!native) {
        return strings;
    }
    if (headOn && !rs7::IsBassKind(kind) && strings == uint32_t(kStockSlots)) {
        return pegLoad;
    }

    const uint32_t cap = modelPegs < uint32_t(kCapacity) ? modelPegs : uint32_t(kCapacity);

    return strings > cap ? cap : strings;
}

unsigned rs7head::ReadyCount(uint8_t* ps) {
    const unsigned loaded = ps[kTail + 1];
    unsigned n = kStockSlots;

    while (n < loaded && n < unsigned(kCapacity)) {
        const uint8_t* slot = Slot(ps, int(n));
        if (!slot[0] || !*reinterpret_cast<uint8_t* const*>(slot + kSlotEntity) ||
            !*reinterpret_cast<uint8_t* const*>(slot + kSlotLocator)) {
            break;
        }

        ++n;
    }

    return n;
}

unsigned rs7head::LoadedCounts(uint8_t* ps) {
    unsigned count = *reinterpret_cast<uint16_t*>(ps + kPegCount);

    if (count > unsigned(kCapacity)) {
        count = kCapacity;
    }
    ps[kTail + 1] = uint8_t(count);

    if (count > unsigned(kStockSlots)) {
        count = kStockSlots;
        *reinterpret_cast<uint16_t*>(ps + kPegCount) = uint16_t(count);
    }

    return count;
}

// ---- the load queue (PSL2 producer, N5 consumer) ----

long rs7head::LoadRing::Reserve() {
    return _InterlockedIncrement(&reserve_) - 1;
}

void rs7head::LoadRing::Publish(long n, const LoadRec& r) {
    Entry& e = ring_[n & 7];

    Rs7Spin_Lock(&e.spin);

    if (e.seq < n + 1) {
        e.rec = r;
        e.seq = n + 1;
    }

    Rs7Spin_Unlock(&e.spin);
}

rs7head::LoadRing::Result rs7head::LoadRing::Pop(LoadRec& out) {
    const long reserve = _InterlockedCompareExchange(&reserve_, 0, 0);
    if (done_ >= reserve) {
        return Empty;
    }

    if (reserve - done_ > 8) {  // the producers lapped the ring: skip to the oldest survivor
        dropped_ += reserve - 8 - done_;
        done_ = reserve - 8;
    }

    Entry& e = ring_[done_ & 7];

    Rs7Spin_Lock(&e.spin);
    const long s1 = e.seq;

    if (s1 != done_ + 1) {
        Rs7Spin_Unlock(&e.spin);

        if (s1 > done_ + 1) {  // overwritten by a later lap
            ++dropped_;
            ++done_;
            return Dropped;
        }

        // reserved, payload not published yet: retry next frame
        return Empty;
    }

    out = e.rec;
    Rs7Spin_Unlock(&e.spin);
    ++done_;

    return Got;
}

// ---- colours and node names ----

bool rs7head::NamedColour(const uint8_t* mgr, uint32_t player, const float* fxRgb, float out[3]) {
    if (!mgr || player >= 2) {
        return false;
    }

    const uint32_t mode = ColourMode(mgr, player);
    if (mode >= 2) {
        return false;
    }

    const float* c1 = *reinterpret_cast<const float* const*>(mgr + rs7colour::PtrOffset(mode, 1));
    const float* c4 = *reinterpret_cast<const float* const*>(mgr + rs7colour::PtrOffset(mode, 4));
    if (!c1 || !c4) {
        return false;
    }

    auto mx = [](const float* c) {
        return c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    };
    const float k = mx(c1) > 0.0f ? mx(c4) / mx(c1) : 1.0f;

    for (int i = 0; i < 3; ++i) {
        out[i] = fxRgb[mode * 3 + i] * k;
    }

    // The peg highlight pass 0x543150 draws this RGB times 2 (times ClipFade): a channel above kPegPeak
    // saturates there and the glow changes hue (00C68E at k = 1 became cyan). Scale the whole colour down
    // instead. The stock named colours logged in game peak at 0.5 (0.5/0.25/0, 0/0.5/0.208).
    if (const float m = mx(out); m > kPegPeak) {
        for (int i = 0; i < 3; ++i) {
            out[i] *= kPegPeak / m;
        }
    }

    return true;
}

bool rs7head::IsTunerNode(const char* name) {
    if (!name) {
        return false;
    }

    char low[48];
    size_t i = 0;

    for (; name[i] && i + 1 < sizeof low; ++i) {
        low[i] = char(std::tolower(static_cast<unsigned char>(name[i])));
    }

    low[i] = 0;

    return std::strstr(low, "arrow") || std::strstr(low, "letter");
}
