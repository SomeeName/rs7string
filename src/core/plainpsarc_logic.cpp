// Pure parts of core/plainpsarc.h (no Win32, no game state): unit-tested by tools/tests/native/plainpsarc.cpp.
#include "core/plainpsarc.h"
#include <cstring>

const char* Rs7_SiteStateName(Rs7SiteState s) {
    switch (s) {
        case Rs7SiteState::Original:
            return "stock";
        case Rs7SiteState::Ours:
            return "ours";
        default:
            return "foreign";
    }
}

Rs7SiteState Rs7_ClassifySite(const uint8_t* live, const uint8_t* original, size_t originalLen, const uint8_t* ours,
                              size_t oursLen) {
    if (std::memcmp(live, original, originalLen) == 0) {
        return Rs7SiteState::Original;
    }
    if (oursLen <= originalLen && std::memcmp(live, ours, oursLen) == 0 &&
        std::memcmp(live + oursLen, original + oursLen, originalLen - oursLen) == 0) {
        return Rs7SiteState::Ours;
    }

    return Rs7SiteState::Foreign;
}

Rs7PlainDecision Rs7Plain_Decide(const Rs7SiteState* states, size_t n) {
    size_t ours = 0;

    for (size_t i = 0; i < n; ++i) {
        if (states[i] == Rs7SiteState::Foreign) {
            return Rs7PlainDecision::Enabler;
        }

        if (states[i] == Rs7SiteState::Ours) {
            ++ours;
        }
    }

    return n && ours == n ? Rs7PlainDecision::Already : Rs7PlainDecision::Patch;
}

Rs7SngAction Rs7Plain_SngAction(uint32_t magic, uint32_t flags, uint32_t len, uint32_t outLen) {
    if (magic != 0x4A || flags != 1) {
        return Rs7SngAction::Decrypt;
    }
    if (len < 0x10 || outLen < len - 0x10) {
        return Rs7SngAction::Reject;
    }

    return Rs7SngAction::Copy;
}

const char* Rs7Plain_ProviderName(Rs7PlainProvider p) {
    switch (p) {
        case Rs7PlainProvider::Off:
            return "off in rs7string ([psarc] plain=0)";
        case Rs7PlainProvider::Ours:
            return "provided by rs7string";
        case Rs7PlainProvider::Enabler:
            return "provided by the CDLC enabler";
        case Rs7PlainProvider::Failed:
            return "not provided (rs7string failed)";
        default:
            return "unknown";
    }
}
