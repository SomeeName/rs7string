// The plain-PSARC logic (psarc.h, no Win32, no game state): unit-tested by tests/psarc/test_psarc.cpp and
// tools/tests/native/plainpsarc_short.cpp.
#include "features/psarc/psarc.h"
#include <cstring>

const char* Rsxr_SiteStateName(RsxrSiteState s) {
    switch (s) {
        case RsxrSiteState::Original:
            return "stock";
        case RsxrSiteState::Ours:
            return "ours";
        default:
            return "foreign";
    }
}

RsxrSiteState Rsxr_ClassifySite(const uint8_t* live, const uint8_t* original, size_t originalLen, const uint8_t* ours,
                                size_t oursLen) {
    if (std::memcmp(live, original, originalLen) == 0) {
        return RsxrSiteState::Original;
    }
    if (oursLen <= originalLen && std::memcmp(live, ours, oursLen) == 0 &&
        std::memcmp(live + oursLen, original + oursLen, originalLen - oursLen) == 0) {
        return RsxrSiteState::Ours;
    }

    return RsxrSiteState::Foreign;
}

RsxrPlainDecision RsxrPlain_Decide(const RsxrSiteState* states, size_t n) {
    size_t ours = 0;

    for (size_t i = 0; i < n; ++i) {
        if (states[i] == RsxrSiteState::Foreign) {
            return RsxrPlainDecision::Enabler;
        }

        if (states[i] == RsxrSiteState::Ours) {
            ++ours;
        }
    }

    return n && ours == n ? RsxrPlainDecision::Already : RsxrPlainDecision::Patch;
}

RsxrSngAction RsxrPlain_SngAction(uint32_t magic, uint32_t flags, uint32_t len, uint32_t outLen) {
    if (magic != 0x4A || flags != 1) {
        return RsxrSngAction::Decrypt;
    }
    if (len < 0x10 || outLen < len - 0x10) {
        return RsxrSngAction::Reject;
    }

    return RsxrSngAction::Copy;
}

const char* RsxrPlain_ProviderName(RsxrPlainProvider p) {
    switch (p) {
        case RsxrPlainProvider::Off:
            return "off in RSXR ([psarc] plain=0)";
        case RsxrPlainProvider::Ours:
            return "provided by RSXR";
        case RsxrPlainProvider::Enabler:
            return "provided by the CDLC enabler";
        case RsxrPlainProvider::Failed:
            return "not provided (RSXR failed)";
        default:
            return "unknown";
    }
}
