#include "features/headstock/assetload_head.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/hooks.h"
#include "features/headstock/state.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

// The pegs of the edited model: the PegSet slots' locators, their placement, and the 3+3 in-line peg swap.

using namespace rs7asset;
using rs7head::g_head;

namespace {
// ---- pegs: the PegSet slots' locator pointers (slot +8, an NiPointer, Rnd_PegSet_BindLocators
// 0x59C980) move to the alt's Locator_Peg_<s+1>, so the stock Rnd_PegSet_ApplyTransforms 0x59C630
// and our per-frame placement put the pegs (and their tuner letters) on the edited model. Slot 6
// (peg 7 of the native PegSet, pegset.h) was bound by the stock binder itself: Locator_Peg_7
// exists only in rs7_alt, so the frame tree's name lookup finds it there; it is checked here too.

uint8_t* PegSet(const Head& h) {
    uint8_t* head = Rs7_SafeRead<uint8_t*>(h.guitar, kGuitarHead);

    return head ? Rs7_SafeRead<uint8_t*>(head, rs7head::kHeadPegs) : nullptr;
}

void SetLocator(uint8_t* slot, uint8_t* node) {  // NiPointer assignment, as 0x59CAD0..
    uint8_t* cur = Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotLocator);
    if (!node || cur == node) {
        return;
    }

    AddRef(node);
    Rs7_SafeStore(slot + rs7head::kSlotLocator, &node, sizeof node);
    if (cur) {
        DropRef(cur);  // still held by its tree
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
    uint8_t* root = alt ? Rs7_SafeRead<uint8_t*>(alt, kParent) : nullptr;  // the clone's top object ("Scene Root")
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
void FindLocators(Head& h) {
    uint8_t* info = Rs7_SafeRead<uint8_t*>(h.guitar, kGuitarInfo);
    // InstrumentInfo +8 = InvertGuitarStrings (CONFIRMED Rnd_InstrumentInfo_FromProfile 0x58ABDB: profile +12h),
    // the byte the stock binder 0x59C980 picks Locator_Invert_Peg_%d by (0x59CA2B); +9 lefty, +0Ah in-line.
    const bool invert = info && Rs7_SafeRead<uint8_t>(info, kInfoInverted) != 0;

    for (int s = 0; s < kSegs + kExtra; ++s) {
        char name[32];

        std::snprintf(name, sizeof name, invert ? "Locator_Invert_Peg_%d" : "Locator_Peg_%d", s + 1);
        uint8_t* loc = FindByName(h.alt, name);

        if (s < kSegs) {
            h.altLoc[s] = loc;
            h.stockLoc[s] = nullptr;
        } else {
            h.altLocX[s - kSegs] = loc;
        }
    }
}

// The stock binder seats the extra slots (6 / 7) on the first Locator_Peg_7 / _8 of the frame, possibly after
// SwapLocators ran: with two models merged (the 8-peg rs7_alt and the 7-peg rs7_alt5) that is the hidden model's.
// Every shown frame: a seated extra slot sits on the shown model's locator.
void KeepExtraLocators(Head& h) {
    uint8_t* pegset = PegSet(h);
    if (!pegset) {
        return;
    }

    const unsigned count = Rs7_SafeRead<uint16_t>(pegset, rs7head::kPegCount);

    for (int e = 0; e < kExtra && kSegs + e < int(count); ++e) {
        uint8_t* slot = rs7head::Slot(pegset, kSegs + e);

        if (h.altLocX[e] && Rs7_SafeRead<uint8_t>(slot, 0) &&
            Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotLocator) != h.altLocX[e]) {
            SetLocator(slot, h.altLocX[e]);
        }
    }
}

void SwapLocators(Head& h, bool toAlt) {
    uint8_t* pegset = PegSet(h);
    if (!pegset) {
        return;
    }

    const unsigned count = Rs7_SafeRead<uint16_t>(pegset, rs7head::kPegCount);
    int n = 0;

    for (int e = 0; e < kExtra && kSegs + e < int(count); ++e) {
        uint8_t* slot = rs7head::Slot(pegset, kSegs + e);

        if (h.altLocX[e] && Rs7_SafeRead<uint8_t>(slot, 0)) {
            SetLocator(slot, h.altLocX[e]);  // the binder's own choice; kept on the alt either way
            ++n;
        }
    }

    for (int s = 0; s < kSegs && s < int(count); ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        if (!Rs7_SafeRead<uint8_t>(slot, 0)) {
            continue;
        }

        if (toAlt) {
            if (!h.altLoc[s]) {
                continue;
            }

            if (!h.stockLoc[s]) {
                h.stockLoc[s] = Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotLocator);
            }
            SetLocator(slot, h.altLoc[s]);
        } else {
            SetLocator(slot, h.stockLoc[s]);
        }

        ++n;
    }

    h.locSwapped = toAlt;

    if (Rs7Log_Upto(&g_head.logPegs, 8)) {
        float w7[3] = {};

        if (toAlt && h.altLocX[0]) {
            Rs7_SafeCopyZero(w7, h.altLocX[0] + 0xA4, sizeof w7);
        }
        Rs7Log("assetload: guitar %p: %d peg locator(s) of PegSet %p moved to the %s model%s (%.3f, %.3f, %.3f)%s",
               h.guitar, n, pegset, toAlt ? "edited" : "stock",
               toAlt ? (h.altLocX[0] ? "; its Locator_Peg_7 (the 7th peg, the B letter) at world"
                                     : "; it has NO Locator_Peg_7")
                     : "",
               double(w7[0]), double(w7[1]), double(w7[2]),
               toAlt && h.altLocX[1] ? "; it has a Locator_Peg_8 (8-string model)" : "");
    }
}

void PlacePegs(Head& h) {
    uint8_t* pegset = PegSet(h);

    if (pegset && Rs7_SafeRead<uint8_t>(pegset, rs7head::kPegLoaded)) {
        rs7head::GamePlacePegs(pegset, Rs7_SafeRead<uint16_t>(pegset, rs7head::kPegCount));
    }
}

void InlinePegs(Head& h) {  // every frame while shown on a non-in-line frame; cheap once done
    uint8_t* pegset = PegSet(h);
    if (!pegset || !Rs7_SafeRead<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    int n = 0, swapped = 0;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        uint8_t* e = Rs7_SafeRead<uint8_t>(slot, 0) ? Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;
        if (!e || h.pegDone[s] == e) {
            continue;
        }

        ++n;

        h.pegDone[s] = e;  // also without a merged peg in this clone: left as it is (logged once)

        if (PegShowInline(e, true)) {
            ++swapped;
            _InterlockedExchange(&g_head.pegSwapEver, 1);
        }
    }

    if (n && Rs7Log_Upto(&g_head.logPegAlt, 6)) {
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
        uint8_t* e = slot ? Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotEntity) : nullptr;

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
    if (!pegset || pegset == h.pegCheckedSet || !Rs7_SafeRead<uint8_t>(pegset, rs7head::kPegLoaded)) {
        return;
    }

    h.pegCheckedSet = pegset;

    for (int s = 0; s < rs7head::kCapacity; ++s) {
        uint8_t* slot = rs7head::Slot(pegset, s);
        uint8_t* e = Rs7_SafeRead<uint8_t*>(slot, rs7head::kSlotEntity);
        uint8_t* root = e ? rs7s7d::EntityRoot(e) : nullptr;
        uint8_t* alt = root ? FindAlt(root) : nullptr;

        if (alt && !(Rs7_SafeRead<uint16_t>(alt, kFlags) & 1)) {
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
        const uint8_t* parent = Rs7_SafeRead<const uint8_t*>(n, kParent);

        if (Rs7_SafeRead<uint16_t>(n, kFlags) & 1) {
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
