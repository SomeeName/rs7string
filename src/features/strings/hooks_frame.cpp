// Per frame (from N5, Rs7_S7Update, game thread): shows or hides the added string objects of a native GuitarStrings
// by raising or lowering its active count (strings.h "Counts").
#include "features/headstock/headstock.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/strings/hooks.h"
#include "model/instrument.h"
#include "platform/log.h"

using namespace rs7gs;

namespace {
// Shown again; its fade state = the strings' (+39h faded in, +38h faded out, else still un-faded like them: the
// coming stock FadeIn covers it, it is in the active count now).
void ShowLikeStrings(uint8_t* gs, uint8_t* obj) {
    if (uint8_t* e = Entity(obj)) {
        GameShow(e, 1);
        if (UncheckedRead<uint8_t>(gs, 0x39)) {
            GameFade(e, true, rs7head::kRaiseFadeIn);
        } else if (UncheckedRead<uint8_t>(gs, 0x38)) {
            GameFade(e, false, 0.05f);
        }
    }
}

// Culls a string and its ghost (the ghost also leaves the scene).
void Hide(uint8_t* main, uint8_t* ghost) {
    if (uint8_t* e = Entity(main)) {
        GameShow(e, 0);
    }

    if (uint8_t* e = Entity(ghost)) {
        GameShow(e, 0);
        GameDetach(e, 1);
    }
}

// C-lite keeps the build count for gameplay; string7_line only changes the added objects' visibility.
// The first frame after a load starts from the stock width, so the added strings fade in with the stock ones.
void FrameBuildCount(uint8_t* guitar, uint8_t* gs, uint8_t* info) {
    const bool bass = rs7::IsBassKind(UncheckedRead<uint8_t>(info, rs7::kInfoKind));
    const uint8_t stock = bass ? 4 : 6;
    const uint8_t now = g_rs7_s7_on ? UncheckedRead<uint8_t>(info, kInfoActive) : stock;
    uint8_t& shown = UncheckedField<uint8_t>(gs, kTail);
    const uint8_t before = shown ? shown : stock;  // first frame: the stock width
    const bool loaded = UncheckedRead<uint8_t>(gs, kTail + 2) != UncheckedRead<uint8_t>(gs, kTail + 1);

    if (now != before && Rs7Log_Upto(&g_strings.logRaise, 6)) {
        Rs7Log("gsnative: guitar %p shows %u strings (was %u)", guitar, now, before);
    }

    for (int s = stock; s < (bass ? 6 : 8); ++s) {
        if (s < now) {
            if (s >= before || loaded) {
                ShowLikeStrings(gs, Child(gs, s, false));
            }
        } else if (s < before || loaded || !shown) {
            Hide(Child(gs, s, false), Child(gs, s, true));
        }
    }

    shown = now;
    UncheckedField<uint8_t>(gs, kTail + 2) = UncheckedRead<uint8_t>(gs, kTail + 1);
}

// The 7th string line: `want` raises the active count to the loaded one, its absence lowers it to the stock count.
void FrameSeventh(uint8_t* guitar, uint8_t* gs, uint8_t* info, bool want) {
    const uint8_t stock = UncheckedRead<uint8_t>(info, rs7::kInfoStrings);
    const uint8_t loaded = UncheckedRead<uint8_t>(info, kInfoLoaded);
    const uint8_t before = UncheckedRead<uint8_t>(info, kInfoActive);
    const uint8_t lo = stock < loaded ? stock : loaded;
    // Lowered only after kLowerFrames frames without the 7th string, as the native PegSet does: a
    // tuner -> song hand-over drops the tuner's mark a few frames before S4 marks the song.
    uint8_t& unwanted = UncheckedField<uint8_t>(gs, kTail + 0);

    if (want) {
        unwanted = 0;
    } else if (unwanted < kLowerFrames) {
        ++unwanted;
    }
    const bool keep = !want && before > lo && unwanted < kLowerFrames;
    const uint8_t visible = g_rs7_publish_ports && loaded > 7 ? 7 : loaded;
    const uint8_t now = keep ? before : want && loaded > kStock ? visible : lo;
    uint8_t* m6 = Child(gs, kStock, false);

    if (now > before) {
        UncheckedField<uint8_t>(info, kInfoActive) = now;
        _InterlockedIncrement(&g_strings.raised);
        _InterlockedIncrement(&g_strings.active7);
        ShowLikeStrings(gs, m6);
        if (Rs7Log_Upto(&g_strings.logRaise, 6)) {
            Rs7Log("gsnative: guitar %p shows the 7th string (active %u -> %u; strings faded in %u, out %u)", guitar,
                   before, now, UncheckedRead<uint8_t>(gs, 0x39), UncheckedRead<uint8_t>(gs, 0x38));
        }
    } else if (now < before) {
        UncheckedField<uint8_t>(info, kInfoActive) = now;
        _InterlockedIncrement(&g_strings.lowered);
        _InterlockedDecrement(&g_strings.active7);
        Hide(m6, Child(gs, kStock, true));
        if (Rs7Log_Upto(&g_strings.logLower, 6)) {
            Rs7Log("gsnative: guitar %p hides the 7th string (active %u -> %u)", guitar, before, now);
        }
    }

    // Inactive: the load's take un-culled string 6 (0x595B40); cull it after every load.
    if (now <= kStock && UncheckedRead<uint8_t>(gs, kTail + 2) != UncheckedRead<uint8_t>(gs, kTail + 1)) {
        UncheckedField<uint8_t>(gs, kTail + 2) = UncheckedRead<uint8_t>(gs, kTail + 1);
        if (uint8_t* e = Entity(m6)) {
            GameShow(e, 0);
        }
        if (Rs7Log_Upto(&g_strings.logLoad, 4)) {
            Rs7Log("gsnative: guitar %p loaded, 7th string culled (not shown)", guitar);
        }
    }
}
}  // namespace

uint8_t* Rs7Gs_Active(uint8_t* gs, int s, bool ghost) {
    if (!g_rs7_native_strings || !IsNative(gs) || s < kStock || s >= kCapacity) {
        return nullptr;
    }

    uint8_t* info = UncheckedRead<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info) || UncheckedRead<uint8_t>(info, kInfoActive) <= s) {
        return nullptr;
    }

    uint8_t* obj = Child(gs, s, ghost);
    if (!obj || !Entity(obj) || !UncheckedRead<uint8_t>(obj, 0x50)) {
        return nullptr;
    }

    return obj;
}

bool Rs7Gs_Frame(uint8_t* guitar, uint8_t* gs, bool want) {
    if (!g_rs7_native_strings || !IsNative(gs)) {
        return false;
    }

    uint8_t* info = UncheckedRead<uint8_t*>(gs, kInfo);
    if (!Rs7Gs_InfoNative(info) || !UncheckedRead<uint8_t>(gs, kTail + 3)) {
        return true;
    }

    RefreshCounts(info, g_rs7_s7_on != 0);
    if (g_rs7_native_publish) {
        FrameBuildCount(guitar, gs, info);
    } else {
        FrameSeventh(guitar, gs, info, want);
    }

    return true;
}
