#include "features/tones/toneplan.h"
#include "features/tones/game.h"
#include "features/tones/tones.h"
#include "model/manifest.h"
#include "platform/log.h"
#include <array>
#include <cstring>

namespace rsxrtones {
namespace {
struct NameView {
    char text[16]{};
    uint32_t end = 0, capacity = 0;

    void Set(const char* value) {
        strcpy_s(text, value);
        end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text) + std::strlen(text));
        capacity = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&end));
    }
};

static_assert(sizeof(NameView) == 0x18);

struct Names {
    std::array<NameView, 27> extensions, fields;

    Names() {
        extensions[0].Set("ToneBase");
        fields[0].Set("Tone_Base");

        for (uint32_t slot = 1; slot < 27; ++slot) {
            char extension[] = "ToneA", field[] = "Tone_A";

            extension[4] = field[5] = static_cast<char>('A' + slot - 1);
            extensions[slot].Set(extension);
            fields[slot].Set(field);
        }
    }
};

struct PlanState {
    Names names;
    uint32_t slots = 0;  // menu/game thread only; the synchronous builder wrap restores nested calls
};

PlanState g_plan;
}  // namespace

const char* SlotExtension(uint32_t slot) {
    return slot < 27 ? g_plan.names.extensions[slot].text : nullptr;
}

uint32_t PlanName(uint32_t slot, bool manifestField) {
    if (slot >= 27) {
        return 0;
    }

    const auto& name = manifestField ? g_plan.names.fields[slot] : g_plan.names.extensions[slot];

    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name));
}

uint32_t PlanSlots(uint32_t attributes) {
    double marker = 0;
    if (!g_rsxr_tones || !attributes ||
        !rsxr::JsonNumber(reinterpret_cast<void*>(uintptr_t(attributes)), "RsxrToneSlots", &marker) || marker != 26) {
        return 0;
    }

    uint32_t slots = 0;

    for (uint32_t slot = 0; slot < 27; ++slot) {
        char key[256]{};

        if (Name(attributes, g_plan.names.fields[slot].text, key, sizeof key) && key[0]) {
            slots |= 1u << slot;
        }
    }

    return (slots & ~31u) ? slots : 0;
}

uint32_t BeginPlan(uint32_t attributes) {
    const uint32_t previous = g_plan.slots;
    const uint32_t slots = PlanSlots(attributes);

    g_plan.slots = slots;

    if (slots) {
        char used[64]{};
        unsigned rows = 0;

        for (uint32_t slot = 0; slot < 27; ++slot) {
            if (!(slots & (1u << slot))) {
                continue;
            }

            if (rows++) {
                strcat_s(used, ",");
            }

            strcat_s(used, slot ? SlotExtension(slot) + 4 : "Base");
        }

        RsxrLog("tones26: toneplan rows=%u (%s used)", rows, used);
    }

    return previous;
}

void EndPlan(uint32_t previous) {
    g_plan.slots = previous;
}

bool PlanActive() {
    return g_plan.slots != 0;
}
}  // namespace rsxrtones
