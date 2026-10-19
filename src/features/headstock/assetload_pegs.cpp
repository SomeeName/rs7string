#include "features/headstock/assetload_head.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// The pegs of the edited model: the PegSet slots' locators, their placement, and the 3+3 in-line peg swap.

using namespace rs7asset;

namespace {
volatile long g_logPegs = 0, g_logPegAlt = 0;

// ---- pegs: the PegSet slots' locator pointers (slot +8, an NiPointer, Rnd_PegSet_BindLocators
// 0x59C980) move to the alt's Locator_Peg_<s+1>, so the stock Rnd_PegSet_ApplyTransforms 0x59C630
// and our per-frame placement put the pegs (and their tuner letters) on the edited model. Slot 6
// (peg 7 of the native PegSet, pegset.h) was bound by the stock binder itself: Locator_Peg_7
// exists only in rs7_alt, so the frame tree's name lookup finds it there; it is checked here too.

uint8_t* PegSet(const Head& h) {
    uint8_t* head = Rd<uint8_t*>(h.guitar, kGuitarHead);

    return head ? Rd<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;
}

void SetLocator(uint8_t* slot, uint8_t* node) {  // NiPointer assignment, as 0x59CAD0..
    uint8_t* cur = Rd<uint8_t*>(slot, rs7head::kSlotLocator);
    if (!node || cur == node) {
        return;
    }

    AddRef(node);
    rs7s7d::SafeStore(slot + rs7head::kSlotLocator, &node, sizeof node);
    if (cur) {
        DropRef(cur);  // still held by its tree
    }
}

bool PlacePegsGuarded(uint8_t* pegset, unsigned count) {  // what 0x59C630 does per slot
    __try {
        for (unsigned s = 0; s < count && s < unsigned(rs7head::kCapacity); ++s) {
            uint8_t* slot = rs7head::Slot(pegset, int(s));
            uint8_t* peg = *reinterpret_cast<uint8_t**>(slot + rs7head::kSlotEntity);
            uint8_t* loc = *reinterpret_cast<uint8_t**>(slot + rs7head::kSlotLocator);
            if (!peg || !loc || !*slot) {
                continue;
            }

            float e[3] = {};

            rs7head::GameEuler(loc + 0x80, e);
            reinterpret_cast<void(__cdecl*)(uint8_t*, const float*)>(g_rs7_fn_setpos)(
                peg, reinterpret_cast<float*>(loc + 0xA4));
            reinterpret_cast<void(__cdecl*)(uint8_t*, const float*)>(g_rs7_fn_setrot)(peg, e);
        }

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- 3+3: the in-line peg in a 7-string guitar's peg clones (kLayouts comment) ----
// Each peg is a pool entity with its own clone of peg_bakelite.nif, which carries the merged in-line peg as
// rs7_alt (culled). Swapped: the bakelite parts culled, rs7_alt shown, the bakelite locator_letter /
// Locator_Arrow renamed (the game looks them up by name, depth-first: UI_PlayerTuner_PlacePegLetters 0x5B1A10
// every frame, the precision widget 0x5B4DE0), so the lookups find rs7_alt's. Undone the same way, also for a
// recycled clone that a stock guitar gets later (StockPegs).
constexpr const char* kLetter = "locator_letter";
constexpr const char* kArrow = "Locator_Arrow";
constexpr const char* kLetterOff = "rs7s_locator_letter";
constexpr const char* kArrowOff = "rs7s_Locator_Arrow";

uint8_t* FindOutside(uint8_t* root, uint8_t* alt, const char* want) {  // in root's children except alt's subtree
    uint8_t* found = nullptr;

    EachChild(root, [&](uint8_t* c) {
        if (c != alt) {
            found = FindByName(c, want);
        }

        return !found;
    });

    return found;
}

// show = true: in-line peg; false: the option's own (stock) peg. Returns whether the clone has an rs7_alt.
bool PegShowInline(uint8_t* entity, bool show) {
    uint8_t* eroot = entity ? rs7s7d::EntityRoot(entity) : nullptr;
    uint8_t* alt = eroot ? FindAlt(eroot) : nullptr;
    uint8_t* root = alt ? Rd<uint8_t*>(alt, kParent) : nullptr;  // the clone's top object ("Scene Root")
    if (!root) {
        return false;
    }

    EachChild(root, [&](uint8_t* c) {
        if (c == alt || !IsNodeOrMesh(c)) {
            return true;
        }

        char nm[32] = {};

        rs7s7d::NameOfNode(c, nm, sizeof nm);
        if (!std::strcmp(nm, kLetter) || !std::strcmp(nm, kArrow) || !std::strcmp(nm, kLetterOff) ||
            !std::strcmp(nm, kArrowOff)) {
            return true;  // locators: renamed, never culled
        }

        SetCulled(c, show);

        return true;
    });

    SetCulled(alt, !show);
    Rename(FindOutside(root, alt, show ? kLetter : kLetterOff), show ? kLetterOff : kLetter);
    Rename(FindOutside(root, alt, show ? kArrow : kArrowOff), show ? kArrowOff : kArrow);

    return true;
}

// Whether a scene node is the rs7_alt part of a 3+3 peg clone.
bool IsAltNode(const uint8_t* node) {
    char name[16] = {};

    rs7s7d::NameOfNode(node, name, sizeof name);

    return !std::strcmp(name, kAltName);
}
}  // namespace

namespace rs7asset {
volatile long g_pegSwapEver = 0;

void FindLocators(Head& h) {
    uint8_t* info = Rd<uint8_t*>(h.guitar, kGuitarInfo);
    // InstrumentInfo +8 = InvertGuitarStrings (CONFIRMED Rnd_InstrumentInfo_FromProfile 0x58ABDB: profile +12h),
    // the byte the stock binder 0x59C980 picks Locator_Invert_Peg_%d by (0x59CA2B); +9 lefty, +0Ah in-line.
    const bool invert = info && Rd<uint8_t>(info, kInfoInverted) != 0;

    for (int s = 0; s < kSegs; ++s) {
        char name[32];

        std::snprintf(name, sizeof name, invert ? "Locator_Invert_Peg_%d" : "Locator_Peg_%d", s + 1);
        h.altLoc[s] = FindByName(h.alt, name);
        h.stockLoc[s] = nullptr;
    }

    h.altLoc7 = FindByName(h.alt, invert ? "Locator_Invert_Peg_7" : "Locator_Peg_7");
}

void SwapLocators(Head& h, bool toAlt) {
    uint8_t* pegset = PegSet(h);
    if (!pegset) {
        return;
    }

    const unsigned count = Rd<uint16_t>(pegset, rs7head::kPegCount);
    int n = 0;

    if (count > unsigned(kSegs) && h.altLoc7 && Rd<uint8_t>(rs7head::Slot(pegset, kSegs), 0)) {
        SetLocator(rs7head::Slot(pegset, kSegs), h.altLoc7);  // the binder's own choice; kept on the alt either way
        ++n;
    }

    for (int s = 0; s < kSegs && s < int(count); ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        if (!Rd<uint8_t>(slot, 0)) {
            continue;
        }

        if (toAlt) {
            if (!h.altLoc[s]) {
                continue;
            }

            if (!h.stockLoc[s]) {
                h.stockLoc[s] = Rd<uint8_t*>(slot, rs7head::kSlotLocator);
            }
            SetLocator(slot, h.altLoc[s]);
        } else {
            SetLocator(slot, h.stockLoc[s]);
        }

        ++n;
    }

    h.locSwapped = toAlt;

    if (Upto(&g_logPegs, 8)) {
        float w7[3] = {};

        if (toAlt && h.altLoc7) {
            rs7s7d::SafeCopy(w7, h.altLoc7 + 0xA4, sizeof w7);
        }
        Rs7Log("assetload: guitar %p: %d peg locator(s) of PegSet %p moved to the %s model%s (%.3f, %.3f, %.3f)",
               h.guitar, n, pegset, toAlt ? "7-string" : "stock",
               toAlt ? (h.altLoc7 ? "; its Locator_Peg_7 (the 7th peg, the B letter) at world"
                                  : "; it has NO Locator_Peg_7")
                     : "",
               double(w7[0]), double(w7[1]), double(w7[2]));
    }
}

void PlacePegs(Head& h) {
    uint8_t* pegset = PegSet(h);
    if (!pegset || !g_rs7_fn_setpos || !g_rs7_fn_setrot || !g_rs7_fn_euler) {
        return;
    }
    if (!Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    PlacePegsGuarded(pegset, Rd<uint16_t>(pegset, rs7head::kPegCount));
}

void InlinePegs(Head& h) {  // every frame while shown on a non-in-line frame; cheap once done
    uint8_t* pegset = PegSet(h);
    if (!pegset || !Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    int n = 0, swapped = 0;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        uint8_t* e = Rd<uint8_t>(slot, 0) ? Rd<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;
        if (!e || h.pegDone[s] == e) {
            continue;
        }

        ++n;

        if (PegShowInline(e, true)) {
            h.pegDone[s] = e;
            ++swapped;
            _InterlockedExchange(&g_pegSwapEver, 1);
        } else {
            h.pegDone[s] = e;  // no merged peg in this clone: leave it (logged once)
        }
    }

    if (n && Upto(&g_logPegAlt, 6)) {
        Rs7Log("assetload: guitar %p (non-in-line frame, 3+3): %d of %d peg(s) of PegSet %p show the in-line peg%s",
               h.guitar, swapped, n, pegset,
               swapped ? ""
                       : " - none: peg_bakelite.nif has no merged rs7_alt (see the "
                         "assetload peg line at its load)");
    }
}

void RestorePegs(Head& h) {
    uint8_t* pegset = PegSet(h);

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = pegset ? rs7head::Slot(pegset, s) : nullptr;
        uint8_t* e = slot ? Rd<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;

        if (e && h.pegDone[s] == e) {
            PegShowInline(e, false);
        }
        h.pegDone[s] = nullptr;
    }
}

// A stock guitar (not shown with the 7-string model): once per loaded PegSet, any peg clone still showing the
// in-line peg (a pool instance recycled from a 7-string guitar) gets its own peg back. Only after a swap ever.
void StockPegs(Head& h) {
    uint8_t* pegset = PegSet(h);
    if (!pegset || pegset == h.pegCheckedSet || !Rd<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    h.pegCheckedSet = pegset;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        uint8_t* e = Rd<uint8_t*>(slot, rs7head::kSlotEntity);
        uint8_t* root = e ? rs7s7d::EntityRoot(e) : nullptr;
        uint8_t* alt = root ? FindAlt(root) : nullptr;

        if (alt && !(Rd<uint16_t>(alt, kFlags) & 1)) {
            PegShowInline(e, false);
        }
    }
}
}  // namespace rs7asset

// PGH1: whether a peg mesh of the highlight pass lies in a part of a 3+3 peg clone that PegShowInline hid: the
// mesh or an ancestor is culled and is rs7_alt or a sibling of it. The pass draws its mesh list (collected at
// setup by NiNode 0x9E1360, which ignores the cull flag) without looking at the flag, so without this the
// hidden bakelite knob (or the hidden in-line knob) got the selected peg's glow. Any other mesh: false.
extern "C" int __cdecl Rs7Asset_PegMeshHidden(const uint8_t* mesh) {
    const uint8_t* n = mesh;

    for (int depth = 0; n && depth < 16; ++depth) {
        const uint8_t* parent = Rd<const uint8_t*>(n, kParent);

        if (Rd<uint16_t>(n, kFlags) & 1) {
            bool hidden = IsAltNode(n);

            if (parent && !hidden) {
                EachChild(parent, [&](const uint8_t* c) {
                    hidden = c != n && IsAltNode(c);

                    return !hidden;
                });
            }

            if (hidden) {
                return 1;
            }
        }

        n = parent;
    }

    return 0;
}

// PGH1 0x5431DC in Rnd_StringRenderer_PegHighlightPass 0x543150 (EBX = the peg mesh, after the slot checks):
// replaces `push 0Eh; fstp [ebp-10h]`. A hidden mesh continues the loop at 0x54338A like the pass's own skips
// (ECX = the index, the loop-invariant 1.0 stays on the FPU stack).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PGH1(Rs7Mid::Context& c) {
    if (Rs7Asset_PegMeshHidden(reinterpret_cast<const uint8_t*>(c.ebx))) {
        c.jump = kPGH1Skip;
    }
}
