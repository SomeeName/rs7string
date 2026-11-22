// The alternate-arrangement list hooks ALTS / ALTF / ALTL / ALTR (arrangements.h).
#include "features/arrangements/arrangements.h"
#include "features/arrangements/menu.h"
#include "features/testdrive/testdrive.h"
#include "model/gamestring.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/patch.h"
#include "platform/safecopy.h"
#include "platform/seh.h"
#include <windows.h>
#include <cstring>

using RsxrMid::Context;
using RsxrMid::Dword;
using namespace rsxrmenu;

extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_ALTS = 0, g_rsxr_target_ALTF = 0, g_rsxr_target_ALTL = 0,
                                           g_rsxr_target_ALTR = 0;

namespace {
constexpr uint32_t kSiteLength = 7;                    // ALTS: lea eax,[esp+10Ch]
constexpr uint32_t kRecordIndex = 0x34;                // ALTS [esp+34h]: record index
constexpr uint32_t kFilledSlots = 0x6C;                // ALTS [esp+6Ch]: slots filled so far
constexpr uint32_t kRecords = 0x388;                   // page +388h/+38Ch: record vector begin/end
constexpr uint32_t kRecordsP2 = 0x394;                 // page +394h/+398h: the second player's record vector
constexpr uint32_t kSlotParent = rsxrmenu::kPageNode;  // parent of the slots
constexpr uint32_t kFindPath = 0x889CA0;               // see FindPath
constexpr uint32_t kSetMember = 0x8816C0;              // stdcall (node, key*, node**)
constexpr uint32_t kPlaynexts = 3;                     // the recommendation buttons playnext_slot1..3
constexpr uint32_t kSelected = 0x3A0;                  // page +3A0h: selected record index
constexpr uint32_t kSlotCounter = 0x3A4;  // page +3A4h: incremented per slot by the builder, reset only on page entry
constexpr uint32_t kPageVtable = 0x011B4B28;  // the Learn a Song page (constructor 0x63D3FD)
constexpr DWORD kLeftAfterMs = 500;           // no page tick for this long: the page was left

struct State {
    uint32_t page = 0;
    uint32_t offset = 0;      // alternates above the first shown entry
    uint32_t alternates = 0;  // records of the page minus the selected one
    uint32_t building = 0;    // slot (1..4) the builder fills now, 0 outside a shown record
    uint32_t shown = 0;       // inside Rebuild: the root of the build on screen (referenced), merged by ALTR
    uint32_t spareFocus[kFocusListDwords] = {};  // the builder's focus lists, kept for its next run (see Rebuild)
    bool focusHooked = false;                    // the page vtable's focus steps lead to FocusUp / FocusDown
    DWORD tick = 0;
};

// The game's string (read by 0x87C180): +0 data (the inline buffer itself while +14h == this + 10h), +10h end,
// +14h capacity end. Ours is in heap mode, pointing at `text`.
struct Label {
    const char* begin;
    char inline_[12];
    const char* end;
    const char* capacityEnd;
    char text[200];
};

State g_alt;
Label g_label;

// The key of slot `slot` (1..kAltSlots), made once by the game's key constructor and kept (never dropped: the drop
// 0x87C3D0 takes ESI).
uint32_t* SlotKey(uint32_t slot) {
    using MakeKey = void(__fastcall*)(const char*, void*, uint32_t*);

    static uint32_t keys[rsxrarr::kAltSlots] = {};
    uint32_t& key = keys[slot - 1];

    if (!key) {
        char name[32];

        wsprintfA(name, "altArrangementsP1_slot%u", slot);
        reinterpret_cast<MakeKey>(uintptr_t(kMakeKey))(name, nullptr, &key);
    }

    return &key;
}

// The key "States", made once like SlotKey's.
uint32_t* StatesKey() {
    using MakeKey = void(__fastcall*)(const char*, void*, uint32_t*);

    static uint32_t key = 0;

    if (!key) {
        reinterpret_cast<MakeKey>(uintptr_t(kMakeKey))("States", nullptr, &key);
    }

    return &key;
}

// Whether the focused button is alternate slot `slot` (1..kAltSlots).
bool FocusIsSlot(uint32_t page, uint32_t slot) {
    char want[48];
    char focus[64];

    wsprintfA(want, "Buttons.altArrangementsP1_slot%u.States", slot);
    ReadFocus(page, focus, sizeof focus);

    return std::strcmp(focus, want) == 0;
}

// 0x889CA0 (ECX = node, EAX = dotted path, both caller-owned): the node at `path` below `node`, borrowed, or 0.
// Inline asm: the path is passed in EAX, which no function pointer type can express.
uint32_t FindPath(uint32_t node, const char* path) {
    uint32_t found = 0;

    if (!node) {
        return 0;
    }

    __asm {
        mov ecx, node
        mov eax, path
        mov edx, kFindPath
        call edx
        mov found, eax
    }

    return found;
}

// The recommendation buttons get their States node (which the focus setter 0x5C89B0 looks up as
// "Buttons.<id>.States" in the root, refusing the move without it) from page entry only; the builder writes the
// buttons anew without it (CONFIRMED in game: arrow Up from the first alternate stuck after a scroll). Take each one
// (referenced, removed from the old button) before the build ...
void TakePlaynextStates(uint32_t buttons, uint32_t (&states)[kPlaynexts]) {
    using Remove = bool(__stdcall*)(uint32_t, uint32_t*);

    for (uint32_t i = 0; i < kPlaynexts; ++i) {
        char path[32];

        wsprintfA(path, "playnext_slot%u", i + 1);
        const uint32_t button = FindPath(buttons, path);

        states[i] = FindPath(button, "States");

        if (states[i]) {
            RefOp(states[i], 0);
            reinterpret_cast<Remove>(uintptr_t(kRemoveMember))(button, StatesKey());
        }
    }
}

// ... and give it to the new button that lacks one (0x8816C0 references and re-parents it).
void GivePlaynextStates(uint32_t buttons, uint32_t (&states)[kPlaynexts]) {
    using Set = void(__stdcall*)(uint32_t, uint32_t*, uint32_t*);

    for (uint32_t i = 0; i < kPlaynexts; ++i) {
        char path[32];

        wsprintfA(path, "playnext_slot%u", i + 1);
        const uint32_t button = FindPath(buttons, path);

        if (button && states[i] && !FindPath(button, "States")) {
            reinterpret_cast<Set>(uintptr_t(kSetMember))(button, StatesKey(), &states[i]);
        }
        RefOp(states[i], 4);
    }
}

// Re-runs the builder the way the movie shows it. The builder fills a fresh clone of the page's view definition
// (0x5C6C40, page +1A0h) and the movie redraws an entry only when its node is new to the tree it reads: the shown
// slots are removed first, ALTR merges the rest of the previous root (the song header) into the clone, and the
// builder adds the slots anew.
void Rebuild(uint32_t page) {
    using Build = void(__thiscall*)(uint32_t);
    using Remove = bool(__stdcall*)(uint32_t, uint32_t*);

    ClearRecords(page + kRecords);
    ClearRecords(page + kRecordsP2);
    Field(page + kSlotCounter) = 0;

    for (uint32_t slot = 1; slot <= rsxrarr::kAltSlots; ++slot) {
        if (*SlotKey(slot)) {
            reinterpret_cast<Remove>(uintptr_t(kRemoveMember))(Dword(page + kSlotParent), SlotKey(slot));
        }
    }

    uint32_t states[kPlaynexts] = {};

    TakePlaynextStates(Dword(page + kSlotParent), states);
    SwapFocusLists(page, g_alt.spareFocus);
    g_alt.shown = Dword(page + kRoot);
    RefOp(g_alt.shown, 0);
    reinterpret_cast<Build>(uintptr_t(Dword(Dword(page) + kBuild)))(page);
    RefOp(g_alt.shown, 4);
    g_alt.shown = 0;
    SwapFocusLists(page, g_alt.spareFocus);
    GivePlaynextStates(Dword(page + kSlotParent), states);
}

// Moves the window one step and shows it: rebuild, then the movie redraws the entries on a focus move (CONFIRMED in
// game, no other trigger found), so the focus moves one step away and back with the stock steps. Returns whether the
// window moved.
bool ScrollStep(uint32_t page, bool down, const char* by) {
    const uint32_t offset = rsxrarr::Scroll(g_alt.offset, g_alt.alternates, down, !down);

    RsxrLog("arrangements: %s, offset %u -> %u of %u alternates", by, g_alt.offset, offset, g_alt.alternates);
    if (offset == g_alt.offset) {
        return false;
    }

    char before[64];
    char moved[64];
    const auto up = reinterpret_cast<FocusStep>(uintptr_t(kStockFocusUp));
    const auto downStep = reinterpret_cast<FocusStep>(uintptr_t(kStockFocusDown));

    g_alt.offset = offset;
    Rebuild(page);
    ReadFocus(page, before, sizeof before);
    up(page, 1);
    ReadFocus(page, moved, sizeof moved);

    downStep(page, 1);
    if (std::strcmp(before, moved) == 0) {  // the first focusable entry: Up stopped there, so step back up
        up(page, 1);
    }

    return true;
}

// The page vtable's focus steps (arrow keys and mouse wheel, game thread): Down on the last alternate shown / Up on the
// first scrolls the window while alternates remain beyond it, like the game's own lists; anything else is the stock
// step.
void __fastcall FocusUp(uint32_t page, void*, int steps) {
    if (!(FocusIsSlot(page, 1) && ScrollStep(page, false, "Up"))) {
        reinterpret_cast<FocusStep>(uintptr_t(kStockFocusUp))(page, steps);
    }
}

void __fastcall FocusDown(uint32_t page, void*, int steps) {
    if (!(FocusIsSlot(page, rsxrarr::kAltSlots) && ScrollStep(page, true, "Down"))) {
        reinterpret_cast<FocusStep>(uintptr_t(kStockFocusDown))(page, steps);
    }
}

// Points the Learn a Song page vtable's focus steps at FocusUp / FocusDown, once, if they still hold the stock steps.
void HookFocusSteps() {
    if (g_alt.focusHooked) {
        return;
    }

    g_alt.focusHooked = true;
    if (!rsxrmenu::HookFocusSteps(kPageVtable, uintptr_t(&FocusUp), uintptr_t(&FocusDown))) {
        RsxrLog("arrangements: focus steps not hooked (vtable +DCh %08X, +E0h %08X); PageUp / PageDown still scroll",
                Dword(kPageVtable + kFocusUp), Dword(kPageVtable + kFocusDown));
    }
}
}  // namespace

// ALTS - 0x63E868: replay the LEA for a record in the window while a slot is free, else join the stock skip path: the
// JZ rel32 that ends at the site jumps to the loop latch 0x63F258, which reloads every register it reads.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_ALTS(Context& c) {
    const uint32_t index = Dword(c.esp + kRecordIndex);
    const uint32_t selected = Dword(c.ebx + kSelected);
    const uint32_t count = RecordCount(c.ebx + kRecords);
    const uint32_t filled = Dword(c.esp + kFilledSlots);

    g_alt.alternates = count > 0 ? count - 1 : 0;

    if (index < count && rsxrarr::AltInWindow(index, selected, g_alt.offset) && rsxrarr::AltSlotFits(filled)) {
        g_alt.building = filled + 1;
        return;
    }

    const uint32_t site = static_cast<uint32_t>(g_rsxr_resume_ALTS) - kSiteLength;

    g_alt.building = 0;
    c.jump = site + Dword(site - 4);
}

// ALTL - 0x63E9A0, before 0x882810 sets the entry's Label from the game string in EDI: the first / last entry get the
// number of hidden alternates appended (a copy; the game's string is untouched and destroyed by its stack address).
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_ALTL(Context& c) {
    char tag[16];

    rsxrarr::LabelTag(g_alt.building, g_alt.offset, g_alt.alternates, tag, sizeof tag);
    // The movie skips an entry whose text equals the one it drew on page entry (CONFIRMED in game: back at offset 0
    // nothing redrew), so a rebuild's labels end in a space.
    if (g_alt.shown) {
        lstrcatA(tag, " ");
    }
    if (!tag[0]) {
        return;
    }

    const size_t tagLen = std::strlen(tag);
    size_t keep = 0;

    rsxr::ReadGameString(RsxrMid::Ptr(c.edi), g_label.text, sizeof g_label.text - tagLen, true, &keep);

    std::memcpy(g_label.text + keep, tag, tagLen + 1);
    g_label.begin = g_label.text;
    g_label.end = g_label.text + keep + tagLen;
    g_label.capacityEnd = g_label.text + sizeof g_label.text - 1;
    c.edi = uint32_t(uintptr_t(&g_label));
}

// ALTR - 0x63E6D3, after 0x5C6C40 (EBX = page) took a new view definition root: on a scroll rebuild the previous
// root's members are merged into it (the song header and the rest the builder does not write; shared nodes), then the
// builder replaces the slots and recommendations as on page entry.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_ALTR(Context& c) {
    using Merge = void(__thiscall*)(uint32_t, uint32_t, uint32_t);

    const uint32_t root = Dword(c.ebx + kRoot);

    if (!g_alt.shown || !root || root == g_alt.shown) {
        return;
    }

    reinterpret_cast<Merge>(uintptr_t(kMerge))(root, g_alt.shown, 1);
}

// ALTF - 0x63DDF9, after the page update's base call 0x5CB7D0 (EDI = the Learn a Song page, game thread; runs only
// while the page opens): a page opened anew starts at the top of the alternates.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_ALTF(Context& c) {
    const uint32_t page = c.edi;
    const DWORD now = GetTickCount();
    const bool entered = page != g_alt.page || now - g_alt.tick > kLeftAfterMs;

    HookFocusSteps();
    g_alt.page = page;
    g_alt.tick = now;
    if (!entered || g_alt.offset == 0) {
        return;
    }

    g_alt.offset = 0;
    if (RecordCount(page + kRecords) > 0) {
        Rebuild(page);
    }
}

// PageDown / PageUp from the game window's procedure, on the game's main thread (the window thread is the render and
// UI thread, logged by the overlay): scroll the window of the Learn a Song page still on screen and re-run its builder.
// ALTF cannot do this, it runs only while the page opens (4 ticks, CONFIRMED in game).
void RsxrArr_KeyDown(unsigned vk) {
    if (vk != VK_NEXT && vk != VK_PRIOR) {
        return;
    }

    char ui[64];
    uint32_t vtable = 0;
    const uint32_t page = g_alt.page;

    if (!RsxrUi_ReadState(kUiStateGlobal, ui, sizeof ui) || std::strcmp(ui, "LearnASong_SongOptions") != 0 || !page ||
        !Rsxr_SafeCopy(&vtable, reinterpret_cast<const void*>(uintptr_t(page)), sizeof vtable) ||
        vtable != kPageVtable) {
        return;
    }

    __try {  // the page can go away between the checks and the builder (no other guard on this thread)
        ScrollStep(page, vk == VK_NEXT, vk == VK_NEXT ? "PageDown" : "PageUp");
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        // A fault inside Rebuild leaves `shown` set (ALTL would keep appending the space and ALTR would merge a stale
        // root on the next build). Its reference and the focus lists swapped with the dead page stay as they are:
        // touching that page again is what just faulted.
        g_alt.page = 0;
        g_alt.shown = 0;
    }
}
