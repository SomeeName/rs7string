#pragma once
#include "platform/hooktable.h"
#include <cstdint>

// Songs with more than five arrangements. The Learn a Song page (las_songoptions.gfx) has four authored
// "other arrangements" buttons, altArrangementsP1_slot1..4: one selected arrangement plus four alternates.
// The page builder 0x63E6B0 formats altArrangementsP1_slot%d for every alternate without a bound, and asking
// for slot 5+ leaves Scaleform with a bogus display node that crashes the renderer (misaligned MULPS at
// 0xCBCBC9). ALTS shows a window of four alternates from a scroll position (0 = the stock first four); songs with
// five or fewer arrangements show every alternate, as before. Enter on an entry starts that arrangement. Arrow Down
// on the last entry shown / Up on the first (and the mouse wheel, the same focus steps) scroll the window like the
// game's own lists: the page vtable's focus steps +DCh / +E0h are swapped (hooks.cpp, the focused id at page +1FCh);
// PageDown / PageUp (window messages, Rs7Arr_KeyDown) scroll too. ALTF resets the window on page entry, and ALTL marks
// the edge entries with the number of hidden alternates.
// A scroll re-runs the builder: it fills a fresh clone of the view definition, so the shown slots are removed first,
// ALTR merges the previous definition (song header) into the clone, the page keeps its focus lists (the builder's
// differ, and Play looks the focus up in them), and a stock focus step away and back redraws the entries.
//   ALTF 0x63DDF9 after the page update's base call (EDI = page, game thread; only while the page opens).
//   ALTR 0x63E6D3 after 0x5C6C40 at the builder's start (EBX = page) took the new definition root (page +1A0h).
//   ALTL 0x63E9A0 the call 0x882810 that sets an entry's "Label" from the game string in EDI (not an MSVC std::string).
//   ALTS 0x63E868 in the alternate loop, after the "not the selected arrangement" test: EBX = page object
//        (+388h/+38Ch record vector, 0FCh stride; +3A0h selected index), [esp+34h] = record index,
//        [esp+6Ch] = slots filled so far. Hidden: skip to the loop latch (the target of the JZ just
//        before the site).

namespace rs7arr {
constexpr uint32_t kAltSlots = 4;  // altArrangementsP1_slot1..4 in las_songoptions.gfx

// Whether one more alternate fits on the page after `filled` slots.
inline bool AltSlotFits(uint32_t filled) {
    return filled < kAltSlots;
}

// Whether record `index` (not the selected one) falls in the kAltSlots alternates shown from scroll position
// `offset`: alternates are the records without `selected`, in record order.
inline bool AltInWindow(uint32_t index, uint32_t selected, uint32_t offset) {
    if (index == selected) {
        return false;
    }

    const uint32_t rank = index > selected ? index - 1 : index;

    return rank >= offset && rank - offset < kAltSlots;
}

// The scroll position after one input: down moves the window while alternates remain below it, up while alternates
// remain above.
inline uint32_t Scroll(uint32_t offset, uint32_t alternates, bool down, bool up) {
    if (down && offset + kAltSlots < alternates) {
        return offset + 1;
    }
    if (up && offset > 0) {
        return offset - 1;
    }

    return offset;
}

// The label suffix of alternate slot `slot` (1..kAltSlots): " (-N)" on the first slot with N alternates above the
// window, " (+N)" on the last slot with N below, else "". Writes at most `n` bytes including the terminator.
void LabelTag(uint32_t slot, uint32_t offset, uint32_t alternates, char* out, size_t n);

// The co-op arrangement list (coop.cpp): every record in one column, of which about nine fit the panel. Songs with
// more than kCoopRows arrangements show a window of kCoopRows records from a scroll position.
constexpr uint32_t kCoopRows = 8;

// Whether record `index` of `count` is shown from scroll position `offset`.
inline bool CoopInWindow(uint32_t index, uint32_t count, uint32_t offset) {
    return count <= kCoopRows || (index >= offset && index - offset < kCoopRows);
}

// The scroll position that shows record `selected`: `offset` kept inside the records and moved just far enough
// (page entry: the selected record; a focus step: the record it moves to).
uint32_t CoopWindowFor(uint32_t offset, uint32_t count, uint32_t selected);

// The record a focus step of `steps` (negative: up) moves to from record `index`, stopping at the ends.
inline uint32_t CoopStep(uint32_t index, uint32_t count, int steps) {
    const int target = int(index) + steps;

    return target < 0 ? 0 : uint32_t(target) >= count ? count - 1 : uint32_t(target);
}

// The record index of a focused button id that names "arrangement<N>" (the builder's node names), or -1.
int CoopFocusIndex(const char* focus);
}  // namespace rs7arr

extern const Rs7HookTable kRs7ArrangementHooks;

// A key press from the game window's procedure (overlay.cpp, the game's main thread): PageDown / PageUp scroll.
void Rs7Arr_KeyDown(unsigned vk);
