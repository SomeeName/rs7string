#include "features/colours/colours.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/pegcolour.h"
#include "features/headstock/state.h"
#include "features/scene/scene.h"
#include "model/groups.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <intrin.h>

// The PegSet glue: what PSC1 / PSL2 / PSN1 / PSN2 call, and N5's per-frame step (raise peg 7, refresh the cached peg
// colours, the head_cull experiment). Game thread except PSC1 / PSL2 (the PegSet ctor / load of Rnd_Guitar_LoadStep,
// on resource-loader jobs): those touch only the PegSet and the counters, no Win32 call, no C++ exception frame.

namespace {
using namespace rsxrhead;

uint8_t* PegSetOf(uint8_t* guitar) {
    uint8_t* head = Rsxr_SafeRead<uint8_t*>(guitar, kGuitarHead);

    return head ? UncheckedField<uint8_t*>(head, kHeadPegs) : nullptr;
}

uint8_t* ColourManager() {  // *X_CM, null before the game made it
    return g_rsxr_colormgr ? *reinterpret_cast<uint8_t**>(g_rsxr_colormgr) : nullptr;
}

// ---- the active count: raise to 7 (8 on an 8-string guitar with the 8-peg model) ----
uint8_t& Loaded(uint8_t* ps) {
    return UncheckedField<uint8_t>(ps, kTail + 1);
}

bool Raise(uint8_t* ps, const char* by) {
    const unsigned count = UncheckedField<uint16_t>(ps, kPegCount);
    if (count >= 7) {
        return true;
    }

    const unsigned target = ReadyCount(ps);

    if (count != 6 || !UncheckedField<uint8_t>(ps, kPegLoaded) || target <= unsigned(kStockSlots)) {
        if (Loaded(ps) >= 7 && RsxrLog_Once(&g_head.logNoSlot)) {
            Rsxr_HeadLog("headstock: PegSet %p: no 7th peg to show (slot 6 %s; the model has no Locator_Peg_7?)", ps,
                         Slot(ps, kStockSlots)[0] ? "taken" : "NOT taken: the stock binder 0x59C980 found no locator",
                         nullptr);
        }

        return false;
    }

    UncheckedField<uint16_t>(ps, kPegCount) = uint16_t(target);
    GamePegSetCall(g_rsxr_fn_pegplace, ps);  // Rnd_PegSet_ApplyTransforms over the raised count
    GamePegSetCall(g_rsxr_fn_peglayer, ps);  // the render layer, as 0x58DD20 does
    // The extra pegs join the PegSet's current fade state, with the call the stock fade loops make per peg.
    const bool out = UncheckedField<uint8_t>(ps, kPegFadedOut) != 0, in = UncheckedField<uint8_t>(ps, kPegFadedIn) != 0;

    for (unsigned s = kStockSlots; s < target; ++s) {
        uint8_t* peg = UncheckedField<uint8_t*>(Slot(ps, int(s)), kSlotEntity);

        GameShowEntity(peg, 1);
        if (out) {
            GameFade(g_rsxr_fn_fadeout, peg, 0.01f);
        } else if (in) {
            GameFade(g_rsxr_fn_fadein, peg, kRaiseFadeIn);
        }
    }

    _InterlockedIncrement(&g_head.active7);

    if (RsxrLog_Upto(&g_head.logRaise, 6)) {
        Rsxr_HeadLog(in || out
                         ? "headstock: PegSet %p: %u pegs active (%s), the extra pegs join the PegSet's fade state"
                         : "headstock: PegSet %p: %u pegs active (%s), the extra pegs (no fade requested yet)",
                     ps, reinterpret_cast<void*>(uintptr_t(target)), by);  // 32-bit: %u reads the pointer-sized count
    }

    return true;
}
}  // namespace

// ---- stub entry points ----
extern "C" void __cdecl Rsxr_PegSetCtor(uint8_t* ps) {
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
// counters; N5 logs (LogLoads).
extern "C" void __cdecl Rsxr_PegSetLoaded(uint8_t* ps) {
    if (!IsNative(ps)) {
        _InterlockedIncrement(&g_head.foreign);
        return;
    }

    const unsigned loaded = UncheckedField<uint16_t>(ps, kPegCount);

    LoadedCounts(ps);

    for (unsigned s = kStockSlots; s < loaded && s < unsigned(kCapacity); ++s) {
        GameShowEntity(UncheckedField<uint8_t*>(Slot(ps, int(s)), kSlotEntity), 0);  // the take showed it (0x595B99)
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
        if (res == LoadRing::Dropped || !RsxrLog_Upto(&g_head.logLoad, 8)) {
            continue;
        }

        Rsxr_HeadLog(r.loaded >= 7 ? "headstock: PegSet %p loaded %u pegs natively (slot 6 %s), 6 active"
                                   : "headstock: PegSet %p loaded %u pegs (the instrument's count)",
                     reinterpret_cast<void*>(r.ps), reinterpret_cast<void*>(uintptr_t(r.loaded)),
                     r.slot6 ? "taken, on its Locator_Peg_7" : "not taken: no Locator_Peg_7 in the model");
    }

    if (g_head.foreign && RsxrLog_Once(&g_head.logForeign)) {
        Rsxr_HeadLog(
            "headstock: %u PegSet load(s) WITHOUT the rsxr layout (constructed before the patch): stock, 6 pegs",
            reinterpret_cast<void*>(uintptr_t(g_head.foreign)), nullptr, nullptr);
    }

    if (g_head.ownFull && RsxrLog_Once(&g_head.logOwnFull)) {
        Rsxr_HeadLog(
            "headstock: PegSet ownership table FULL (%u PegSets left stock, %u owned): investigate a free that "
            "bypasses PSA2",
            reinterpret_cast<void*>(uintptr_t(g_head.ownFull)), reinterpret_cast<void*>(uintptr_t(OwnedCount())),
            nullptr);
    }

    if (g_head.ctors && RsxrLog_Once(&g_head.logCtor)) {
        Rsxr_HeadLog("headstock: PegSets constructed natively so far: %u (8 slots, pegColor at +14Ch, size 1D4h)",
                     reinterpret_cast<void*>(uintptr_t(g_head.ctors)), nullptr, nullptr);
    }
}
}  // namespace

extern "C" void __cdecl Rsxr_PegNamed(uint8_t* session, uint32_t index) {
    uint8_t* guitar = session ? UncheckedField<uint8_t*>(session, kSessionGuitar) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    if (!IsNative(ps) || index >= unsigned(kCapacity) || index >= UncheckedField<uint16_t>(ps, kPegCount)) {
        return;
    }

    uint8_t* slot = Slot(ps, int(index));
    if (!slot[0]) {
        return;
    }

    float rgb[3];
    float palette[6];

    if (!NamedColour(ColourManager(), UncheckedField<uint32_t>(session, kSessionPlayer),
                     rsxrpegcolour::ExtraPalette(index, palette), rgb)) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        UncheckedField<float>(slot, kSlotRgb + 4 * uint32_t(i)) = rgb[i];
    }

    UncheckedField<uint32_t>(slot, kSlotNamed) =
        2 + index;  // string colour index (2..7 = strings 0..5 at 0x59BF80; 8, 9 ours)
}

void RsxrHead_TrackColourPlayer(uint8_t* session) {
    uint8_t* guitar = session ? UncheckedField<uint8_t*>(session, kSessionGuitar) : nullptr;
    uint8_t* ps = PegSetOf(guitar);
    const unsigned player = session ? UncheckedField<uint32_t>(session, kSessionPlayer) : 2;

    if (IsNative(ps) && player < 2) {
        ps[kColourPlayer] = uint8_t(player);
    }
}

// ---- per frame ----
namespace {
// The cached named colours of a native PegSet follow the colour manager (palette / colour-blind mode changes).
void RefreshColours(uint8_t* ps) {
    const uint8_t* manager = ColourManager();
    const unsigned changed = rsxrpegcolour::Refresh(ps, manager, ps[kColourPlayer]);

    if (changed && g_head.colourLogs < 8) {
        ++g_head.colourLogs;
        RsxrLog("pegcolour: refreshed %u cached peg colour(s), player %u (manager %p, PegSet %p)", changed,
                unsigned(ps[kColourPlayer]), static_cast<const void*>(manager), static_cast<void*>(ps));

        for (unsigned s = 0; s < UncheckedField<uint16_t>(ps, kPegCount); ++s) {
            uint8_t* slot = Slot(ps, int(s));

            if (slot[0] && UncheckedField<uint32_t>(slot, kSlotNamed) >= 2) {
                RsxrLog("pegcolour: selected slot %u name %u RGB %.3f/%.3f/%.3f outline phase %.3f/%.3f", s,
                        UncheckedField<uint32_t>(slot, kSlotNamed), UncheckedField<float>(slot, kSlotRgb),
                        UncheckedField<float>(slot, kSlotRgb + 4), UncheckedField<float>(slot, kSlotRgb + 8),
                        UncheckedField<float>(slot, 0x20), UncheckedField<float>(slot, 0x24));
            }
        }
    }
}
}  // namespace

void RsxrHead_Frame(uint8_t* guitar, bool want) {
    if (!g_rsxr_native_pegset) {
        return;
    }

    LogLoads();
    uint8_t* ps = PegSetOf(guitar);
    if (!IsNative(ps) || !UncheckedField<uint8_t>(ps, kPegLoaded)) {
        return;
    }

    RefreshColours(ps);
    // C-lite: a guitar built with 7 or 8 strings keeps its extra pegs for its whole life (raised once, after the
    // load completed: the extra slots are loaded for every guitar but active, i.e. in the stock loops, only on these).
    if (want && Loaded(ps) >= 7 && UncheckedField<uint16_t>(ps, kPegCount) == 6 && g_rsxr_head_on) {
        Raise(ps, "N5: the guitar was built with 7 or 8 strings");
    }
    if (want) {
        DebugFrame(Rsxr_SafeRead<uint8_t*>(guitar, kGuitarHead));
    }
}

void RsxrHead_Stats(RsxrHeadStats* out) {
    RsxrHeadStats st{};

    st.pegsets = int(g_head.ctors);
    st.loaded7 = int(g_head.loads7);
    st.active7 = int(g_head.active7);
    st.liveOwned = g_head.liveOwned != 0;
    *out = st;
}

void RsxrHead_ResetLog() {
    for (volatile long* f : {&g_head.logOwnFull, &g_head.logCtor, &g_head.logLoad, &g_head.logRaise, &g_head.logNoSlot,
                             &g_head.logForeign}) {
        _InterlockedExchange(f, 0);
    }
}
