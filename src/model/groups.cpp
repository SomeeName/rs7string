#include "model/groups.h"

#include <cstddef>

const char* Rsxr_FeatureName(RsxrFeature f) {
    static const char* const n[] = {"core",
                                    "native colours",
                                    "native PegSet",
                                    "native GuitarStrings",
                                    "native FretGrid",
                                    "native pitch storage",
                                    "native next-note grid",
                                    "[diagrams]",
                                    "[test] private extras",
                                    "[debug] diagnostics",
                                    "native publication ports",
                                    "chord loops",
                                    "native tuner",
                                    "[psarc] plain",
                                    "C-lite",
                                    "RSMods glow correction",
                                    "menu loading retry"};
    static_assert(sizeof n / sizeof n[0] == size_t(RsxrFeature::Count), "one name per RsxrFeature");

    return size_t(f) < size_t(RsxrFeature::Count) ? n[size_t(f)] : "?";
}

bool Rsxr_IsSevenUnit(RsxrFeature f) {
    switch (f) {
        case RsxrFeature::Colours:
        case RsxrFeature::Pegs:
        case RsxrFeature::Strings:
        case RsxrFeature::Fretgrid:
        case RsxrFeature::Pitch:
        case RsxrFeature::Nextgrid:
        case RsxrFeature::Publish:
        case RsxrFeature::Tuner:
        case RsxrFeature::Clite:
            return true;
        default:
            return false;
    }
}
