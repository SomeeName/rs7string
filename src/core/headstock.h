#pragma once
#include <cstdint>

// The headstock's tuning pegs as a native N-slot PegSet (issue #18 stage 3,
// docs/ARCHITECTURE.md). The PegSet (Rnd_Headstock +0Ch) is allocated, constructed,
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
//   +1CCh rs7 tail: u8 capacity (8), u8 loaded count, u8 frames without the 7th string, u8 frames
//        to the cull of the lowered pegs; +1D0h magic. Size 1D4h (PSA1 / PSA2).
// Identification (review #1): a PegSet is native only if it is in the ownership table g_rs7_pegsets
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
// Active count: 6 for a guitar built with 6 strings (peg 7 loaded, culled, outside every stock loop) and
// for bass (4); 7 for a guitar built with 7 or 8 strings (C-lite, N5 `want` = InstrumentInfo +0Bh >= 7,
// from the first frame after the load). Raising: count 7, peg 7 shown, placed and layered by the stock
// functions, its fade set to the PegSet's state (+6 faded in / +7 faded out) with the stock per-peg
// ClipFade call. The count never goes back down: a guitar's string count is fixed for its life (a
// different count is a different guitar). headstock_peg=0 (6) never changes. A model without
// Locator_Peg_7 (the stock binder marks slot 6 not taken) has no 7th peg.

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_head_on;  // [neck] headstock_peg (default 1)
// The native PegSet (a member of the native 7-string unit since 1.0; the stage-2 mirrored peg is gone). Set by
// Rs7_Startup before any site is patched; cleared only by a failing startup check (then nothing is patched).
__declspec(dllexport) extern uintptr_t g_rs7_native_pegset;
__declspec(dllexport) extern uintptr_t g_rs7_peg_load;  // PSL1: slots a 6-string guitar loads (7)
__declspec(dllexport) extern uintptr_t
    g_rs7_fn_euler;  // X_EULER 0x8FCAA0 ESI = &rotate 3x3, EDI = &e[2], (&e[0], &e[1]) ret 8
__declspec(dllexport) extern uintptr_t g_rs7_fn_pegplace;   // X_PEGPLACE 0x59C630 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rs7_fn_peglayer;   // X_PEGLAYER 0x59C6E0 stdcall(PegSet)
__declspec(dllexport) extern uintptr_t g_rs7_pegnames;      // X_PEGNAMES the six colour names 0x12ED0C0
__declspec(dllexport) extern uintptr_t g_rs7_fn_pegselect;  // X_PEGSELECT 0x5B0880 ECX = session, (index) ret 4
// Native PegSet ownership table (review #1): 0 = free entry. Any thread reads it (the PS* mid-hook
// callbacks use IsNative / Disown, PSU1 / PSU2 / PSM1 scan it in asm: Rs7_PegOwnedZF in hooks/stubs.cpp); entries are
// claimed with a compare-exchange (PSC1, resource-loader jobs) and cleared only by the owner's free (PSA2). No
// allocation.
#define RS7_PEG_OWNERS 64
__declspec(dllexport) extern uintptr_t g_rs7_pegsets[RS7_PEG_OWNERS];
__declspec(dllexport) void __cdecl Rs7_PegSetCtor(uint8_t* pegset);                 // PSC1
__declspec(dllexport) void __cdecl Rs7_PegSetLoaded(uint8_t* pegset);               // PSL2
__declspec(dllexport) void __cdecl Rs7_PegNamed(uint8_t* session, uint32_t index);  // PSN2 (index >= 6)
}

namespace rs7head {
constexpr uint32_t kGuitarHead = 0x18;  // Rnd_Guitar: Rnd_Headstock*
constexpr uint32_t kHeadFrame = 0x04;   // Rnd_Headstock: frame entity (MeshGuitarFrame*)
constexpr uint32_t kHeadPegs = 0x0C;    // Rnd_Headstock: PegSet*
constexpr uint32_t kPegCount = 0x04;    // PegSet: u16 active count
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
constexpr uint32_t kTail = 0x1CC;  // u8 capacity, u8 loaded, u8 frames unwanted, u8 cull countdown
constexpr uint32_t kMagicOff = 0x1D0;
constexpr uint32_t kMagic = 0x50375352;  // "RS7P"
constexpr uint32_t kSize = 0x1D4;
constexpr float kRaiseFadeIn = 0.75f;  // the tuner's / guitar's stock fade-in duration (0x5AEB50)

// Peg load queue (PSL2 producer on a resource-loader job, N5 consumer on the game thread). Entries hold
// copied diagnostic values only, never a PegSet to dereference later. Each entry is published last (seqlock:
// 0 while being written, then index+1); a consumer that finds an entry overwritten or torn counts it dropped.
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
        volatile long seq;
        LoadRec rec;
    };

    volatile long reserve_ = 0;
    long done_ = 0, dropped_ = 0;
    Entry ring_[8] = {};
};

// Ownership (review #1, g_rs7_pegsets). Own: register (already registered = true); false = table full
// (the caller leaves the PegSet stock). Disown: remove; false = was not registered. IsNative: registered.
// None of them dereferences the PegSet.
constexpr unsigned kOwners = RS7_PEG_OWNERS;
bool Own(const uint8_t* pegset) noexcept;
bool Disown(const uint8_t* pegset) noexcept;
unsigned OwnedCount() noexcept;
void ResetOwners() noexcept;  // tests only
bool IsNative(const uint8_t* pegset) noexcept;

// PegSet helpers (pure, no game calls): slot address.
inline uint8_t* Slot(uint8_t* pegset, int s) {
    return pegset + kSlots + uint32_t(s) * kSlotStride;
}

// The stock ctor's layout -> the native one (PSC1's body without game calls): slots 6/7 take
// slot 0's fresh defaults, pegColor[0..5] move from +0FCh to +14Ch, [6]/[7] = [0], tail + magic.
void InitTail(uint8_t* pegset);
// What PSL2 does to the counts: loaded = active count after the stock load; a guitar that loaded
// 7 starts with 6 active. Returns the active count.
unsigned LoadedCounts(uint8_t* pegset);
// The named colour of a string beyond the stock six (the selected knob tint of 0x5B0880): the
// [color] string6 colour scaled like the stock string 0's named colour (colour state 4) is to its
// lit colour (state 1). colorMgr = the colour manager, player 0/1. False without it.
bool NamedColour(const uint8_t* colorMgr, uint32_t player, const float* fxRgb, float out[3]);
// Whether a peg node name belongs to the tuner arrow / letter.
bool IsTunerNode(const char* name);
}  // namespace rs7head

// Startup: the ini [debug] head_cull / head_stretch are re-read from (nullptr / empty = never).
void Rs7Head_SetIni(const wchar_t* path);
// Overlay (core/settings.h, any thread): [debug] head_cull, head_stretch, head_stretch_mode. The next
// N5 frame of a 7-string guitar takes them; from the first call on, the ini is no longer re-read.
void Rs7Head_SetLive(const char* cull, float stretch, int mode);

// Overlay status: native PegSets constructed, loads with a 7th peg, raises of peg 7 (counted since startup).
struct Rs7HeadStats {
    int pegsets, loaded7, active7;
    bool liveOwned;
};

void Rs7Head_Stats(Rs7HeadStats* out);
// From N5 (Rs7_S7Update) every frame, game thread: want = the guitar was built with 7/8 strings.
void Rs7Head_Frame(uint8_t* guitar, bool want);
// The peg entity of slot s while s is below the active count and taken (nullptr otherwise).
uint8_t* Rs7Head_Peg(uint8_t* guitar, int s);
void Rs7Head_ResetLog();
