// The chord feature's settings: [chords] wide_misses (overlay and template). hooks.cpp reads it from the store at
// startup (RsxrChord_Configure); ApplyMisses applies live changes.
#include "platform/settings.h"
#include "features/chords/chords.h"
#include "features/nslot/nslot.h"
#include "platform/uilayout.h"

namespace {
using T = RsxrSetType;
using W = RsxrSetWhen;

void ApplyMisses(const RsxrSetValue& v) {
    g_rsxr_match_misses = static_cast<uintptr_t>(v.i);
}  // the matcher reads it per call (nslot.h)

const RsxrSetDef kDefs[] = {
    {"chords.wide_misses", L"chords", L"wide_misses", T::Int, W::Live, "2", 0, 2, "Wide chord misses",
     "Notes that may be missing when matching 7- and 8-note chord requests, on any string (0-2). 5- and 6-note "
     "chords allow one miss; 2-4-note chords allow none, as in stock.",
     ApplyMisses},
};
}  // namespace

const RsxrSetTable kRsxrChordSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), nullptr};

namespace {
const RsxrUiItem kUi[] = {
    RsxrUiPlayer("chords.wide_misses", "Missed notes in big chords",
                 "How many notes of a seven- or eight-note chord may be missed (0-2)."),
};
}  // namespace

const RsxrUiTable kRsxrChordUi = {kUi, int(sizeof kUi / sizeof kUi[0])};
