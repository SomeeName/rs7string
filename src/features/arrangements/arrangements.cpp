// Logic of the alternate-arrangement list (arrangements.h): no game addresses, no registers.
#include "features/arrangements/arrangements.h"
#include <cstdio>
#include <cstring>

namespace rsxrarr {
void LabelTag(uint32_t slot, uint32_t offset, uint32_t alternates, char* out, size_t n) {
    out[0] = 0;

    const uint32_t below = alternates > offset + kAltSlots ? alternates - offset - kAltSlots : 0;

    if (slot == 1 && offset > 0) {
        std::snprintf(out, n, " (-%u)", offset);
    } else if (slot == kAltSlots && below > 0) {
        std::snprintf(out, n, " (+%u)", below);
    }
}

uint32_t CoopWindowFor(uint32_t offset, uint32_t count, uint32_t selected) {
    if (count <= kCoopRows) {
        return 0;
    }

    if (offset > count - kCoopRows) {
        offset = count - kCoopRows;
    }
    if (selected < count && selected < offset) {
        offset = selected;
    } else if (selected < count && selected - offset >= kCoopRows) {
        offset = selected - kCoopRows + 1;
    }

    return offset;
}

int CoopFocusIndex(const char* focus) {
    const char* name = std::strstr(focus, "arrangement");

    if (!name) {
        return -1;
    }

    const char* digit = name + std::strlen("arrangement");
    int index = 0;

    if (*digit < '0' || *digit > '9') {
        return -1;
    }

    for (; *digit >= '0' && *digit <= '9' && index < 100000; ++digit) {
        index = index * 10 + (*digit - '0');
    }

    return index;
}
}  // namespace rsxrarr
