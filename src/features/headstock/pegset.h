#pragma once
#include "features/headstock/headstock.h"
#include "model/instrument.h"
#include <cstdint>

// The headstock's tuning pegs as a native N-slot PegSet (docs/ARCHITECTURE.md): its layout and the
// logic over it (pegset.cpp, values only, no game call). The PegSet (Rnd_Headstock +0Ch) is allocated, constructed,
// loaded, unloaded and destroyed with capacity 8; every stock per-slot operation (placement
// 0x59C630, render layer 0x59C6E0, fade out/in 0x59C800 / 0x59C8C0, locator binding 0x59C980, mesh
// preparation 0x59CBC0, the knob highlight 0x543150, the glow quads 0x544610, colour reset 0x5B0960
// / select 0x5B0880, the precision widget 0x5B4DE0) iterates the active count word [PegSet+4], so
// peg 7 is one more stock slot. No peg is mirrored.
//
// Layout (stock prefix up to the slots kept):
//   +0 pool id, +4 u16 active count, +6/+7 fade in/out requested, +8 loaded,
//   +0Ch slots[8] x 28h (stock: 6; +0 taken, +4 entity, +8 locator NiPointer, +0Ch owning pointer,
//        +10h rgb, +1Ch named colour index, +20h/+24h message scalars),
//   +14Ch pegColor[8] x 10h (stock: 6 at +0FCh, written by the stock ctor, moved by PSC1),
//   +1CCh rsxr tail: u8 capacity (8), u8 loaded count, u8 colour player, u8 reserved;
//        +1D0h magic. Size 1D4h (PSA1 / PSA2).
// Identification: a PegSet is native only if it is in the ownership table g_rsxr_pegsets
// (registered by PSC1 before InitTail, removed by PSA2 right before the sized free). Never by the
// tail/magic: a PegSet constructed before the patch is a stock 0x15C allocation, and +1CCh/+1D0h lie
// beyond it. The magic is still written, for diagnostics only.
// Sites: PSA1/PSA2 allocation and sized free, PSC1 ctor tail, PSD1/PSD2 dtor (8 slots, stock
// release order), PSU1-PSU3 unload (every slot with an entity, 8 taken flags), PSM1/PSM2 renderer
// mesh collection (8 slots by taken flag), PSQ1 pool request (guitar: 7 pegs), PSL1 load count (a
// 6-string guitar loads 7 slots: take, Locator_Peg_7 from the stock binder, mesh preparation), PSL2
// load complete (active count 6, peg 7 culled), PSR1 colour reset over the active count, PSN1/PSN2
// colour select of peg index >= 6.
//
// Active count: 6 for a guitar built with 6 strings (peg 7 loaded, culled, outside every stock loop), the
// string count for a bass (4-6); 7 (8 with the 8-peg model) for a guitar built with 7 or 8 strings (C-lite, N5
// `want` = InstrumentInfo +0Bh >= 7, from the first frame after the load). Raising: the extra pegs shown, placed and
// layered by the stock functions, their fade set to the PegSet's state (+6 faded in / +7 faded out) with the stock
// per-peg ClipFade call. The count never goes back down: a guitar's string count is fixed for its life (a different
// count is a different guitar). A model without Locator_Peg_7 (the stock binder marks slot 6 not taken) has no 7th
// peg.

extern "C" {
// Native PegSet ownership table: 0 = free entry. Any thread reads it (the PS* mid-hook
// callbacks use IsNative / Disown); entries are
// claimed with a compare-exchange (PSC1, resource-loader jobs) and cleared only by the owner's free (PSA2). No
// allocation.
#define RSXR_PEG_OWNERS 64
__declspec(dllexport) extern uintptr_t g_rsxr_pegsets[RSXR_PEG_OWNERS];
}

namespace rsxrhead {
using rsxr::kGuitarHead;
constexpr uint32_t kHeadFrame = 0x04;  // Rnd_Headstock: frame entity (MeshGuitarFrame*)
constexpr uint32_t kHeadPegs = 0x0C;   // Rnd_Headstock: PegSet*
constexpr uint32_t kPegCount = 0x04;   // PegSet: u16 active count
constexpr uint32_t kPegFadedIn = 0x06;
constexpr uint32_t kPegFadedOut = 0x07;
constexpr uint32_t kPegLoaded = 0x08;
constexpr uint32_t kSlots = 0x0C;
constexpr uint32_t kSlotStride = 0x28;
constexpr uint32_t kSlotEntity = 0x04;
constexpr uint32_t kSlotLocator = 0x08;
constexpr uint32_t kSlotRgb = 0x10;
constexpr uint32_t kSlotNamed = 0x1C;
constexpr int kStockSlots = 6;
constexpr int kCapacity = 8;

constexpr uint32_t kStockColours = 0xFC;  // pegColor[6] x 10h, where the stock ctor writes it
constexpr uint32_t kColours = 0x14C;      // pegColor[8] x 10h
constexpr uint32_t kColourStride = 0x10;
constexpr uint32_t kTail = 0x1CC;              // u8 capacity, u8 loaded, u8 colour player, u8 reserved
constexpr uint32_t kColourPlayer = kTail + 2;  // player of the last colour selection
constexpr uint32_t kMagicOff = 0x1D0;
constexpr uint32_t kMagic = 0x50585352;  // "RSXP"
constexpr uint32_t kSize = 0x1D4;

// Peg load queue (PSL2 producer on a resource-loader job, N5 consumer on the game thread). Entries hold
// copied diagnostic values only, never a PegSet to dereference later. Each entry has a short lock and a generation:
// delayed producers cannot overwrite a newer lap, and the consumer copies only a fully published record.
struct LoadRec {
    uintptr_t ps;
    uint32_t loaded;
    uint32_t slot6;
};

class LoadRing {
public:
    enum Result { Empty = 0, Got = 1, Dropped = 2 };

    long Reserve();                          // claim the next index (producer, step 1)
    void Publish(long n, const LoadRec& r);  // write the payload, then publish the index (step 2)

    void Push(const LoadRec& r) { Publish(Reserve(), r); }

    Result Pop(LoadRec& out);  // Dropped: one record was lost to overrun, call again

    long DroppedCount() const { return dropped_; }

private:
    struct Entry {
        volatile long spin;
        volatile long seq;
        LoadRec rec;
    };

    volatile long reserve_ = 0;
    long done_ = 0, dropped_ = 0;
    Entry ring_[8] = {};
};

// Ownership (g_rsxr_pegsets). Own: register (already registered = true); false = table full
// (the caller leaves the PegSet stock). Disown: remove; false = was not registered. IsNative: registered.
// None of them dereferences the PegSet.
constexpr unsigned kOwners = RSXR_PEG_OWNERS;
bool Own(const uint8_t* pegset) noexcept;
bool Disown(const uint8_t* pegset) noexcept;
unsigned OwnedCount() noexcept;
void ResetOwners() noexcept;  // tests only
bool IsNative(const uint8_t* pegset) noexcept;

// PegSet helpers (pure, no game calls): slot address.
inline uint8_t* Slot(uint8_t* pegset, int s) {
    return pegset + kSlots + uint32_t(s) * kSlotStride;
}

// LT0: the record an empty slot s of the tuner letters borrows: the first taken slot of the PegSet (count u16 at +4),
// starting at slot 3 for string 5 (a bass's high C, next to the G) and at slot 0 otherwise (the low B next to the E).
// The record's address, 0 = none taken.
inline uint32_t LetterSeat(const uint8_t* pegset, uint32_t s) {
    const unsigned count = pegset ? *reinterpret_cast<const uint16_t*>(pegset + kPegCount) : 0;
    const unsigned first = s == 5 ? rsxr::kBassNeighbour[5] : 0;

    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* slot = pegset + kSlots + (first + i) % count * kSlotStride;

        if (*slot) {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(slot));
        }
    }

    return 0;
}

// The stock ctor's layout -> the native one (PSC1's body without game calls): slots 6/7 take
// slot 0's fresh defaults, pegColor[0..5] move from +0FCh to +14Ch, [6]/[7] = [0], tail + magic.
void InitTail(uint8_t* pegset);
// PSL1's load count from the instrument's string count: a 6-string guitar on a native PegSet loads pegLoad
// (g_rsxr_peg_load, 7); more strings load at most modelPegs (the merged model's Locator_Peg_1..n: 7, or 8 with
// eight strings), an 8th peg on a model without Locator_Peg_8 would have nowhere to sit. A stock PegSet: stock.
uint32_t LoadCount(uint32_t strings, bool native, bool headOn, uint32_t pegLoad, uint32_t modelPegs, int kind);
// The active count N5 raises a loaded native PegSet to: the stock six plus every consecutive extra slot up to
// the loaded count that is taken, has its entity and sits on a locator. 6 = nothing to raise.
unsigned ReadyCount(uint8_t* pegset);
// What PSL2 does to the counts: loaded = active count after the stock load; a guitar that loaded
// 7 starts with 6 active. Returns the active count.
unsigned LoadedCounts(uint8_t* pegset);
// The colour manager's colorMode[player] (0x519680; 0 normal, 1 colour-blind). mgr non-null, player 0/1.
constexpr uint32_t kColourModeOff = 0x348;

inline uint32_t ColourMode(const uint8_t* mgr, uint32_t player) {
    return *reinterpret_cast<const uint32_t*>(mgr + kColourModeOff + 4 * player);
}

// The tuner session (0x5B0880 / 0x5B0960, ESI): its guitar and player.
constexpr uint32_t kSessionGuitar = 0x2C;
constexpr uint32_t kSessionPlayer = 0x08;
// The named colour of a string beyond the stock six (the selected knob tint of 0x5B0880): the
// [color] string6 colour scaled like the stock string 0's named colour (colour state 4) is to its
// lit colour (state 1). colorMgr = the colour manager, player 0/1. False without it.
// Its brightest channel is capped at kPegPeak so that the pass's doubling keeps the hue.
constexpr float kPegPeak = 0.5f;
bool NamedColour(const uint8_t* colorMgr, uint32_t player, const float* fxRgb, float out[3]);
// Whether a peg node name belongs to the tuner arrow / letter.
bool IsTunerNode(const char* name);
}  // namespace rsxrhead
