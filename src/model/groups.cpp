#include "model/groups.h"

const char* Rs7_FeatureName(Rs7Feature f) {
    static const char* const n[] = {"core",
                                    "native colours",
                                    "native PegSet",
                                    "native GuitarStrings",
                                    "native FretGrid",
                                    "native pitch storage",
                                    "native next-note grid",
                                    "[diagrams]",
                                    "[test] vguitar",
                                    "[debug] diagnostics",
                                    "native publication ports",
                                    "[chords] native_chordloops",
                                    "native tuner",
                                    "[psarc] plain",
                                    "C-lite",
                                    "RSMods glow correction"};

    return size_t(f) < size_t(Rs7Feature::Count) ? n[size_t(f)] : "?";
}

bool Rs7_IsSevenUnit(Rs7Feature f) {
    switch (f) {
        case Rs7Feature::Colours:
        case Rs7Feature::Pegs:
        case Rs7Feature::Strings:
        case Rs7Feature::Fretgrid:
        case Rs7Feature::Pitch:
        case Rs7Feature::Nextgrid:
        case Rs7Feature::Publish:
        case Rs7Feature::Tuner:
        case Rs7Feature::Clite:
            return true;
        default:
            return false;
    }
}
