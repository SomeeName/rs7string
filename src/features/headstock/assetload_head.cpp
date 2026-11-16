#include "features/headstock/assetload_head.h"
#include "features/allocstats/allocstats.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/assetload_tex.h"
#include "features/headstock/state.h"
#include "features/strings/strings.h"
#include "platform/log.h"
#include "platform/safecopy.h"
#include <windows.h>
#include <cstring>

// The edited headstock per guitar (game thread, N5): which frame instance, which stock parts are culled, and the
// hand-over to the segment and peg steps.

using namespace rs7asset;
using rs7head::g_head;

namespace {
Head* Slot(uint8_t* guitar) {
    for (Head& h : g_head.heads) {
        if (h.guitar == guitar) {
            return &h;
        }
    }

    for (Head& h : g_head.heads) {
        if (!h.guitar) {
            h = Head{};
            h.guitar = guitar;
            Rs7Alloc_Inc(kAkHeadSlots);
            return &h;
        }
    }

    return nullptr;
}

void FreeSlot(Head& h) {
    ReleaseParams(h);
    h = Head{};
    Rs7Alloc_Dec(kAkHeadSlots);
}

void Restore(Head& h) {
    if (h.pointed) {
        PointParams(h, false);
    }

    if (h.locSwapped) {
        SwapLocators(h, false);
        PlacePegs(h);
    }

    for (int i = 0; i < h.nsibs; ++i) {
        SetCulled(h.sibs[i], false);
    }

    h.nsibs = 0;
    if (h.alt) {
        SetCulled(h.alt, true);
    }
    if (h.onNonInline) {
        RestorePegs(h);
    }
    h.onNonInline = false;
    h.applied = false;
}

void Show(Head& h) {  // every frame while wanted: alt shown, its stock siblings culled
    EachChild(Rs7_SafeRead<uint8_t*>(h.alt, kParent), [&](uint8_t* c) {
        if (c == h.alt || !IsNodeOrMesh(c)) {
            return true;  // effects stay
        }
        if (Rs7_SafeRead<uint16_t>(c, kFlags) & 1) {
            return true;  // already culled (by the game or by us)
        }

        bool known = false;

        for (int k = 0; k < h.nsibs; ++k) {
            known = known || h.sibs[k] == c;
        }

        if (!known) {
            if (h.nsibs >= 8) {
                return true;  // list full: a part culled here could never be restored, so it stays shown
            }

            h.sibs[h.nsibs++] = c;
        }

        SetCulled(c, true);

        return true;
    });

    SetCulled(h.alt, false);
    h.applied = true;

    if (!h.bound) {
        FindSegments(h);
        FindLocators(h);
        BindAlt(h);
    }

    if (!h.pointed) {
        PointParams(h, true);
    }
    if (!h.locSwapped) {
        SwapLocators(h, true);
    }

    if (h.locSwapped) {
        KeepExtraLocators(h);
        PlacePegs(h);
    }

    uint8_t* info = Rs7_SafeRead<uint8_t*>(h.guitar, kGuitarInfo);

    h.onNonInline =
        info && Rs7_SafeRead<uint8_t>(info, kInfoInline) == 0;  // InstrumentInfo +0Ah in-line (CONFIRMED 0x58ABEC)
    // The in-line peg swap is the 3+3 guitar's (peg_bakelite); a 2+2 bass (+0 = 2) keeps its own pegs on our model.
    if (h.onNonInline && !rs7::IsBassKind(Rs7_SafeRead<uint8_t>(info, rs7::kInfoKind))) {
        InlinePegs(h);
    }
    // Native strings: the stock segment colour loop does it for strings 6/7 (GSHC, features/strings/strings.h).
    const bool nativeColour = g_rs7_native_strings && rs7gs::IsNative(Rs7_SafeRead<uint8_t*>(h.guitar, kGuitarStrings));
    const unsigned strings = info ? Rs7_SafeRead<uint8_t>(info, kInfoStrings) : 0;

    LayerBass(h);

    for (int e = 0; e < kExtra; ++e) {
        if (!h.segX[e]) {
            continue;
        }

        // The 8-string model on a 7-string guitar: string_size_8 has no string to follow.
        SetCulled(h.segX[e], !ExtraShown(e, strings));
        if (!nativeColour) {
            ColourExtra(h, e);
        }
        LayerExtra(h, e);
    }

    if (!h.texChecked) {
        h.texChecked = true;
        if (Rs7Log_Upto(&g_head.logFrameCheck, 1)) {
            LogTextureList("first show");
        }
    }
}
}  // namespace

int Rs7Asset_LiveHeads() {
    int n = 0;

    for (const Head& h : g_head.heads) {
        n += h.guitar != nullptr;
    }

    return n;
}

// The guitar is being released (Rs7_S7Free from the native Rnd_GuitarStrings_Free copy, called from the guitar's
// release 0x592770 with guitar+0Ch still its strings): its state slot goes back to the table. Without
// this every guitar object the game ever builds keeps a slot (also the stock-headstock ones: Slot() runs
// before the `!want` early-out), and the 9th distinct address silently ends the edited headstock.
void Rs7Asset_ForgetStrings(const uint8_t* gs) {
    if (!gs) {
        return;
    }

    for (Head& h : g_head.heads) {
        if (!h.guitar) {
            continue;
        }

        uint8_t* strings = nullptr;
        if (!Rs7_SafeCopyZero(&strings, h.guitar + kGuitarStrings, sizeof strings) || strings != gs) {
            continue;
        }

        FreeSlot(h);
    }
}

void Rs7Asset_ForgetGuitar(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    Rs7Head_ForgetWidth(guitar);

    for (Head& h : g_head.heads) {
        if (h.guitar != guitar) {
            continue;
        }

        FreeSlot(h);
    }
}

void Rs7Asset_HeadFrame(uint8_t* guitar, bool want) {
    if (!g_rs7_asset_on || !guitar) {
        return;
    }

    uint8_t* head = Rs7_SafeRead<uint8_t*>(guitar, kGuitarHead);
    uint8_t* frame = head ? Rs7_SafeRead<uint8_t*>(head, kHeadFrame) : nullptr;
    Head* h = Slot(guitar);
    if (!h) {
        return;
    }

    if (!want && !h->applied) {  // every stock frame: two compares (three after a 3+3 peg swap)
        if (g_head.pegSwapEver) {
            StockPegs(*h);
        }
        return;
    }

    uint8_t* root = frame ? rs7s7d::EntityRoot(frame) : nullptr;
    const uint8_t* info = Rs7_SafeRead<uint8_t*>(guitar, kGuitarInfo);
    // One string fewer than the frame's first model: a 5-string bass, or a 7-string guitar (its frame may carry the
    // 8-peg model first); without a rs7_alt5 in the frame the first model stays.
    const uint8_t count = info ? Rs7_SafeRead<uint8_t>(info, kInfoStrings) : 0;
    const bool five = info && (rs7::IsBassKind(Rs7_SafeRead<uint8_t>(info, rs7::kInfoKind)) ? count == 5 : count == 7);

    if (root != h->root || five != h->alt5) {  // new frame instance (or none) or bass count: forget the old pointers
        if (root == h->root && h->applied) {
            Restore(*h);  // the same frame switches models: the old one culled, its stock parts back
        }
        ReleaseParams(*h);
        h->root = root;
        h->alt5 = five;
        h->alt = !root ? nullptr : five && FindAlt(root, kAltName5) ? FindAlt(root, kAltName5) : FindAlt(root);
        h->nsibs = 0;
        h->applied = false;
        h->bound = false;
        h->pointed = false;
        h->locSwapped = false;
        std::memset(h->altLocX, 0, sizeof h->altLocX);
        h->texChecked = false;
        h->frames = 0;
        if (want && !h->alt && root && Rs7Log_Upto(&g_head.logNoAlt, 4)) {
            Rs7Log("assetload: guitar %p (frame entity %p, root %p) has no %s node: no edited model merged for it",
                   guitar, frame, root, kAltName);
        }
    }

    if (!h->alt) {
        return;
    }

    if (want) {
        Show(*h);

        if (++h->frames == 1 && Rs7Log_Upto(&g_head.logFrame, 8)) {
            char sib[48] = {};

            if (h->nsibs) {
                rs7s7d::NameOfNode(h->sibs[0], sib, sizeof sib);
            }

            Rs7Log("assetload: guitar %p shows the edited headstock (%s %p in frame root %p; %d stock part(s) culled, "
                   "first \"%s\"; InstrumentInfo inverted %u lefty %u in-line %u)",
                   guitar, h->alt5 ? kAltName5 : kAltName, h->alt, root, h->nsibs, sib,
                   unsigned(Rs7_SafeRead<uint8_t>(info, kInfoInverted)),
                   unsigned(Rs7_SafeRead<uint8_t>(info, kInfoLefty)),
                   unsigned(Rs7_SafeRead<uint8_t>(info, kInfoInline)));
        }
    } else {
        Restore(*h);
        h->frames = 0;
        if (Rs7Log_Upto(&g_head.logFrame, 8)) {
            Rs7Log("assetload: guitar %p back to the stock headstock", guitar);
        }
    }
}

extern "C" int __cdecl Rs7Asset_LetterInline(const uint8_t* info) {
    const int own = info ? Rs7_SafeRead<uint8_t>(info, kInfoInline) : 0;
    if (own || !info || !g_rs7_asset_on) {
        return own;
    }

    int r = 0;

    for (const Head& h : g_head.heads) {
        if (h.guitar && h.applied && h.onNonInline && Rs7_SafeRead<const uint8_t*>(h.guitar, kGuitarInfo) == info) {
            r = 1;
        }
    }

    if ((info != g_head.lastLetterInfo || r != g_head.lastLetterResult) && Rs7Log_Upto(&g_head.logLetters, 8)) {
        Rs7Log("assetload: LT1 tuner letters of InstrumentInfo %p (in-line byte 0): %s", info,
               r ? "the guitar shows the 7-string in-line headstock - in-line letter rule" : "stock 3+3 rule");
    }
    g_head.lastLetterInfo = info;
    g_head.lastLetterResult = r;

    return r;
}

uint8_t* Rs7Asset_SegColour(uint8_t* guitar, uint32_t string) {
    const uint32_t e = string - uint32_t(kSegs);
    if (!g_rs7_asset_on || !guitar || string < uint32_t(kSegs) || e >= uint32_t(kExtra)) {
        return nullptr;
    }

    for (Head& h : g_head.heads) {
        if (h.guitar == guitar) {
            return (h.applied && h.segX[e]) ? reinterpret_cast<uint8_t*>(h.paramsX[e][3]) : nullptr;
        }
    }

    return nullptr;
}
