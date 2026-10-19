#include "features/colours/colours.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegcolour.h"
#include "features/headstock/state.h"
#include "features/scene/scene.h"
#include "model/groups.h"
#include "platform/log.h"
#include <intrin.h>

// The PegSet glue: what PSC1 / PSL2 / PSN1 / PSN2 call, and N5's per-frame step (raise peg 7, refresh the cached peg
// colours, the debug experiments). Game thread except PSC1 / PSL2 (the PegSet ctor / load of Rnd_Guitar_LoadStep,
// on resource-loader jobs): those touch only the PegSet and the counters, no Win32 call, no C++ exception frame.

namespace {
using namespace rs7head;
using rs7asset::Upto;

uint8_t* PegSetOf(uint8_t* guitar) {
    uint8_t* head = guitar ? At<uint8_t*>(guitar, kGuitarHead) : nullptr;

    return head ? At<uint8_t*>(head, kHeadPegs) : nullptr;
}

// ---- the active count: raise to 7 ----
uint8_t& Loaded(uint8_t* ps) {
    return At<uint8_t>(ps, kTail + 1);
}

bool Raise(uint8_t* ps, const char* by) {
    const unsigned count = At<uint16_t>(ps, kPegCount);
    if (count >= 7) {
        return true;
    }

    uint8_t* slot = Slot(ps, kStockSlots);
    uint8_t* peg = At<uint8_t*>(slot, kSlotEntity);

    if (count != 6 || Loaded(ps) < 7 || !At<uint8_t>(ps, kPegLoaded) || !slot[0] || !peg ||
        !At<uint8_t*>(slot, kSlotLocator)) {
        if (Loaded(ps) >= 7 && Once(&g_head.logNoSlot)) {
            Rs7_HeadLog("headstock: PegSet %p: no 7th peg to show (slot 6 %s; the model has no Locator_Peg_7?)", ps,
                        slot[0] ? "taken" : "NOT taken: the stock binder 0x59C980 found no locator", nullptr);
        }
        return false;
    }

    At<uint16_t>(ps, kPegCount) = 7;
    GameShowEntity(peg, 1);
    GamePegSetCall(g_rs7_fn_pegplace, ps);  // Rnd_PegSet_ApplyTransforms over the seven
    GamePegSetCall(g_rs7_fn_peglayer, ps);  // the render layer, as 0x58DD20 does
    // Peg 7 joins the PegSet's current fade state, with the call the stock fade loops make per peg.
    const bool out = At<uint8_t>(ps, kPegFadedOut) != 0, in = At<uint8_t>(ps, kPegFadedIn) != 0;

    if (out) {
        GameFade(g_rs7_fn_fadeout, peg, 0.01f);
    } else if (in) {
        GameFade(g_rs7_fn_fadein, peg, kRaiseFadeIn);
    }
    _InterlockedIncrement(&g_head.active7);

    if (Upto(&g_head.logRaise, 6)) {
        Rs7_HeadLog(in || out ? "headstock: PegSet %p: 7 pegs active (%s), peg 7 %p joins the PegSet's fade state"
                              : "headstock: PegSet %p: 7 pegs active (%s), peg 7 %p (no fade requested yet)",
                    ps, by, peg);
    }

    return true;
}
}  // namespace

// ---- stub entry points ----
extern "C" void __cdecl Rs7_PegSetCtor(uint8_t* ps) {
    if (!ps) {
        return;
    }

    // Registered before the layout changes; a full table leaves this PegSet entirely stock (6 slots,
    // pegColor at +0FCh): every stub then keeps the stock counts. Its free at PSA2 is then 15Ch for PSA1's
    // 1D4h block (HYPOTHESIS: harmless for the game allocator; only reachable with 64 live native PegSets).
    if (!Own(ps)) {
        _InterlockedIncrement(&g_head.ownFull);
        return;
    }

    InitTail(ps);
    _InterlockedIncrement(&g_head.ctors);
}

// PSC1 / PSL2 run inside the resource jobs of the guitar load: they only touch the PegSet and
// counters; N5 logs (LogLoaded). (The boot crash first blamed on a PSL2 log line overrunning a job
// stack was PSL2's old jmp site, which 0x59C587 jumped into: PSL2 is a call site now, hooks_mid.cpp.)
extern "C" void __cdecl Rs7_PegSetLoaded(uint8_t* ps) {
    if (!IsNative(ps)) {
        _InterlockedIncrement(&g_head.foreign);
        return;
    }

    const unsigned loaded = At<uint16_t>(ps, kPegCount);

    LoadedCounts(ps);

    for (unsigned s = kStockSlots; s < loaded && s < unsigned(kCapacity); ++s) {
        GameShowEntity(At<uint8_t*>(Slot(ps, int(s)), kSlotEntity), 0);  // the take showed it (0x595B99)
    }

    if (loaded >= 7) {
        _InterlockedIncrement(&g_head.loads7);
    }
    LoadRec r{reinterpret_cast<uintptr_t>(ps), loaded, Slot(ps, kStockSlots)[0] ? 1u : 0u};

    g_head.loads.Push(r);
}

namespace {
void LogLoads() {  // N5, game thread: the loads PSL2 recorded since the last frame
    LoadRec r;

    for (;;) {
        const LoadRing::Result res = g_head.loads.Pop(r);
        if (res == LoadRing::Empty) {
            break;
        }
        if (res == LoadRing::Dropped || !Upto(&g_head.logLoad, 8)) {
            continue;
        }

        Rs7_HeadLog(r.loaded >= 7 ? "headstock: PegSet %p loaded %p pegs natively (slot 6 %s), 6 active"
                                  : "headstock: PegSet %p loaded %p pegs (the instrument's count)",
                    reinterpret_cast<void*>(r.ps), reinterpret_cast<void*>(uintptr_t(r.loaded)),
                    r.slot6 ? "taken, on its Locator_Peg_7" : "not taken: no Locator_Peg_7 in the model");
    }

    if (g_head.foreign && Once(&g_head.logForeign)) {
        Rs7_HeadLog("headstock: %p PegSet load(s) WITHOUT the rs7 layout (constructed before the patch): stock, 6 pegs",
                    reinterpret_cast<void*>(uintptr_t(g_head.foreign)), nullptr, nullptr);
    }

    if (g_head.ownFull && Once(&g_head.logOwnFull)) {
        Rs7_HeadLog("headstock: PegSet ownership table FULL (%p PegSets left stock, %p owned): investigate a free that "
                    "bypasses PSA2",
                    reinterpret_cast<void*>(uintptr_t(g_head.ownFull)),
                    reinterpret_cast<void*>(uintptr_t(OwnedCount())), nullptr);
    }

    if (g_head.ctors && Once(&g_head.logCtor)) {
        Rs7_HeadLog("headstock: PegSets constructed natively so far: %p (8 slots, pegColor at +14Ch, size 1D4h)",
                    reinterpret_cast<void*>(uintptr_t(g_head.ctors)), nullptr, nullptr);
    }
}
}  // namespace

extern "C" void __cdecl Rs7_PegNamed(uint8_t* session, uint32_t index) {
    uint8_t* guitar = session ? At<uint8_t*>(session, 0x2C) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    if (!IsNative(ps) || index >= unsigned(kCapacity) || index >= At<uint16_t>(ps, kPegCount)) {
        return;
    }

    uint8_t* slot = Slot(ps, int(index));
    if (!slot[0]) {
        return;
    }

    float rgb[3];
    uint8_t* mgr = g_rs7_colormgr ? *reinterpret_cast<uint8_t**>(g_rs7_colormgr) : nullptr;
    if (!NamedColour(mgr, At<uint32_t>(session, 8), g_rs7_fx_rgb, rgb)) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        At<float>(slot, kSlotRgb + 4 * uint32_t(i)) = rgb[i];
    }

    At<uint32_t>(slot, kSlotNamed) = 2 + index;  // string colour index (2..7 = strings 0..5 at 0x59BF80)
}

void Rs7Head_TrackColourPlayer(uint8_t* session) {
    uint8_t* guitar = session ? At<uint8_t*>(session, 0x2C) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    const unsigned player = session ? At<uint32_t>(session, 8) : 2;

    if (IsNative(ps) && player < 2) {
        ps[kColourPlayer] = uint8_t(player);
    }
}

// ---- per frame ----
namespace {
// The cached named colours of a native PegSet follow the colour manager (palette / colour-blind mode changes).
void RefreshColours(uint8_t* ps) {
    const uint8_t* manager = g_rs7_colormgr ? *reinterpret_cast<uint8_t**>(g_rs7_colormgr) : nullptr;
    const unsigned changed = rs7pegcolour::Refresh(ps, manager, ps[kColourPlayer]);
    static unsigned colourLogs = 0;

    if (changed && colourLogs < 8) {
        ++colourLogs;
        Rs7Log("pegcolour: refreshed %u cached peg colour(s), player %u (manager %p, PegSet %p)", changed,
               unsigned(ps[kColourPlayer]), static_cast<const void*>(manager), static_cast<void*>(ps));

        for (unsigned s = 0; s < At<uint16_t>(ps, kPegCount); ++s) {
            uint8_t* slot = Slot(ps, int(s));

            if (slot[0] && At<uint32_t>(slot, kSlotNamed) >= 2) {
                Rs7Log("pegcolour: selected slot %u name %u RGB %.3f/%.3f/%.3f outline phase %.3f/%.3f", s,
                       At<uint32_t>(slot, kSlotNamed), At<float>(slot, kSlotRgb), At<float>(slot, kSlotRgb + 4),
                       At<float>(slot, kSlotRgb + 8), At<float>(slot, 0x20), At<float>(slot, 0x24));
            }
        }
    }
}
}  // namespace

void Rs7Head_Frame(uint8_t* guitar, bool want) {
    if (!g_rs7_native_pegset) {
        return;
    }

    LogLoads();
    uint8_t* head = guitar ? At<uint8_t*>(guitar, kGuitarHead) : nullptr;
    uint8_t* ps = head ? At<uint8_t*>(head, kHeadPegs) : nullptr;
    if (!IsNative(ps) || !At<uint8_t>(ps, kPegLoaded)) {
        return;
    }

    RefreshColours(ps);
    // C-lite: a guitar built with 7 strings has 7 pegs for its whole life (raised once, after the load
    // completed: the peg-7 slot is loaded for every guitar but active, i.e. in the stock loops, only on these).
    if (want && Loaded(ps) >= 7 && At<uint16_t>(ps, kPegCount) == 6 && g_rs7_head_on) {
        Raise(ps, "N5: the guitar was built with 7 strings");
    }
    if (want) {
        DebugFrame(guitar, head, ps);
    }
}

void Rs7Head_Stats(Rs7HeadStats* out) {
    Rs7HeadStats st{};

    st.pegsets = int(g_head.ctors);
    st.loaded7 = int(g_head.loads7);
    st.active7 = int(g_head.active7);
    st.liveOwned = g_head.liveOwned != 0;
    *out = st;
}

void Rs7Head_ResetLog() {
    for (volatile long* f :
         {&g_head.logCtor, &g_head.logLoad, &g_head.logRaise, &g_head.logNoSlot, &g_head.logForeign}) {
        _InterlockedExchange(f, 0);
    }
}
