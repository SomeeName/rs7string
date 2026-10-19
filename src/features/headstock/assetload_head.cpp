#include "features/headstock/assetload_head.h"
#include "features/allocstats/allocstats.h"
#include "features/headstock/assetload.h"
#include "features/headstock/assetload_scene.h"
#include "features/headstock/assetload_tex.h"
#include "features/strings/strings.h"
#include "platform/log.h"
#include <windows.h>
#include <cstring>

// The edited headstock per guitar (game thread, N5): which frame instance, which stock parts are culled, and the
// hand-over to the segment and peg steps.

using namespace rs7asset;

namespace {
volatile long g_logFrameCheck = 0, g_logNoAlt = 0, g_logFrame = 0, g_logLetters = 0;
Head g_heads[8];

Head* Slot(uint8_t* guitar) {
    for (Head& h : g_heads) {
        if (h.guitar == guitar) {
            return &h;
        }
    }

    for (Head& h : g_heads) {
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
    EachChild(Rd<uint8_t*>(h.alt, kParent), [&](uint8_t* c) {
        if (c == h.alt || !IsNodeOrMesh(c)) {
            return true;  // effects stay
        }
        if (Rd<uint16_t>(c, kFlags) & 1) {
            return true;  // already culled (by the game or by us)
        }

        bool known = false;

        for (int k = 0; k < h.nsibs; ++k) {
            known = known || h.sibs[k] == c;
        }

        if (!known && h.nsibs < 8) {
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
        PlacePegs(h);
    }
    uint8_t* info = Rd<uint8_t*>(h.guitar, kGuitarInfo);

    h.onNonInline = info && Rd<uint8_t>(info, kInfoInline) == 0;  // InstrumentInfo +0Ah in-line (CONFIRMED 0x58ABEC)
    if (h.onNonInline) {
        InlinePegs(h);
    }
    // Native strings: the stock segment colour loop does it for string 6 (GSHC, features/strings/strings.h).
    if (h.seg7 && !(g_rs7_native_strings && rs7gs::IsNative(Rd<uint8_t*>(h.guitar, kGuitarStrings)))) {
        Colour7(h);
    }
    if (h.seg7) {
        Layer7(h);
    }

    if (!h.texChecked) {
        h.texChecked = true;
        if (Upto(&g_logFrameCheck, 1)) {
            LogTextureList("first show");
        }
    }
}
}  // namespace

int Rs7Asset_LiveHeads() {
    int n = 0;

    for (const Head& h : g_heads) {
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

    for (Head& h : g_heads) {
        if (!h.guitar) {
            continue;
        }

        uint8_t* strings = nullptr;
        if (!rs7s7d::SafeCopy(&strings, h.guitar + kGuitarStrings, sizeof strings) || strings != gs) {
            continue;
        }

        FreeSlot(h);
    }
}

void Rs7Asset_ForgetGuitar(const uint8_t* guitar) {
    if (!guitar) {
        return;
    }

    for (Head& h : g_heads) {
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

    uint8_t* head = Rd<uint8_t*>(guitar, kGuitarHead);
    uint8_t* frame = head ? Rd<uint8_t*>(head, kHeadFrame) : nullptr;
    Head* h = Slot(guitar);
    if (!h) {
        return;
    }

    if (!want && !h->applied) {  // every stock frame: two compares (three after a 3+3 peg swap)
        if (g_pegSwapEver) {
            StockPegs(*h);
        }
        return;
    }

    uint8_t* root = frame ? rs7s7d::EntityRoot(frame) : nullptr;

    if (root != h->root) {  // new frame instance (or none): forget the old pointers
        h->root = root;
        h->alt = root ? FindAlt(root) : nullptr;
        h->nsibs = 0;
        h->applied = false;
        h->bound = false;
        h->pointed = false;
        h->locSwapped = false;
        h->altLoc7 = nullptr;
        h->texChecked = false;
        std::memset(h->params7, 0, sizeof h->params7);  // the old frame's objects (refs leaked, never freed)
        h->frames = 0;
        if (want && !h->alt && root && Upto(&g_logNoAlt, 4)) {
            Rs7Log("assetload: guitar %p (frame entity %p, root %p) has no %s node: no edited model merged for it",
                   guitar, frame, root, kAltName);
        }
    }

    if (!h->alt) {
        return;
    }

    if (want) {
        Show(*h);

        if (++h->frames == 1 && Upto(&g_logFrame, 8)) {
            char sib[48] = {};

            if (h->nsibs) {
                rs7s7d::NameOfNode(h->sibs[0], sib, sizeof sib);
            }
            uint8_t* info = Rd<uint8_t*>(guitar, kGuitarInfo);

            Rs7Log("assetload: guitar %p shows the edited headstock (%s %p in frame root %p; %d stock part(s) culled, "
                   "first \"%s\"; InstrumentInfo inverted %u lefty %u in-line %u)",
                   guitar, kAltName, h->alt, root, h->nsibs, sib, unsigned(Rd<uint8_t>(info, kInfoInverted)),
                   unsigned(Rd<uint8_t>(info, kInfoLefty)), unsigned(Rd<uint8_t>(info, kInfoInline)));
        }
    } else {
        Restore(*h);
        h->frames = 0;
        if (Upto(&g_logFrame, 8)) {
            Rs7Log("assetload: guitar %p back to the stock headstock", guitar);
        }
    }
}

extern "C" int __cdecl Rs7Asset_LetterInline(const uint8_t* info) {
    const int own = info ? Rd<uint8_t>(info, kInfoInline) : 0;
    if (own || !info || !g_rs7_asset_on) {
        return own;
    }

    int r = 0;

    for (const Head& h : g_heads) {
        if (h.guitar && h.applied && h.onNonInline && Rd<const uint8_t*>(h.guitar, kGuitarInfo) == info) {
            r = 1;
        }
    }

    static const uint8_t* lastInfo = nullptr;
    static int lastR = -1;

    if ((info != lastInfo || r != lastR) && Upto(&g_logLetters, 8)) {
        Rs7Log("assetload: LT1 tuner letters of InstrumentInfo %p (in-line byte 0): %s", info,
               r ? "the guitar shows the 7-string in-line headstock - in-line letter rule" : "stock 3+3 rule");
    }
    lastInfo = info;
    lastR = r;

    return r;
}

uint8_t* Rs7Asset_Seg7Colour(uint8_t* guitar) {
    if (!g_rs7_asset_on || !guitar) {
        return nullptr;
    }

    for (Head& h : g_heads) {
        if (h.guitar == guitar) {
            return (h.applied && h.seg7) ? reinterpret_cast<uint8_t*>(h.params7[3]) : nullptr;
        }
    }

    return nullptr;
}
