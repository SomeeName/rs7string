// The co-op arrangement list hooks COAT / COAS (arrangements.h). The co-op song page's "other arrangements" list (UI
// state LearnASong_SongOptions_Arrangement, page vtable 0x11B4DD0, builder 0x645F50) shows every record in one column;
// about nine fit the panel and the rest are drawn below it, focusable but invisible (CONFIRMED in game,
// docs/research/backlog/arrangements.md). The movie keeps one row per entry node and places it by the node's
// SortOrder: removing nodes left their rows drawn and new nodes were not drawn (CONFIRMED in game), so every entry
// stays and only its SortOrder moves. With more than kCoopRows records, COAT gives the records in the window the rows
// 0..kCoopRows-1 and the others a row below the panel. The page's focus steps (arrow keys, the mouse wheel) go by
// record and scroll the window by re-running the builder (Step).
// Enter selects map[position of the focus in the focus list] (page +380h, filled by the builder with the record
// index, read by the select handler 0x647470), but the list is in record order only while the window is at the top:
// COAS takes the record from the focused entry's name ("arrangement<record index>") instead.
//   COAT 0x64637C after the FILD of the entry's SortOrder (ST0; [esp+18h] page, [esp+38h] record index).
//   COAS 0x6474DF in the select handler, the store of the selected record (ECX) to page (EBX) +37Ch.
// Known: after a scroll the header's song length shows "1:001" for "1:00" (with or without merging the previous
// definition root into the new one as ALTR does; cause not found).
#include "features/arrangements/arrangements.h"
#include "features/arrangements/coop_state.h"
#include "features/arrangements/menu.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/patch.h"

using Rs7Mid::Context;
using Rs7Mid::Dword;
using namespace rs7menu;
using rs7arr::PageState;

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_COAT = 0, g_rs7_resume_COAS = 0;

namespace {
constexpr uint32_t kPageVtable = 0x011B4DD0;  // the co-op arrangement list page (constructor 0x645D73)
// CONFIRMED 0x645E00: vtable slot 0, scalar deleting destructor, thiscall(page, flags), ret 4.
constexpr uint32_t kDestroy = 0x645E00;
constexpr uint32_t kRecords = 0x370;     // page +370h/+374h: record vector begin/end
constexpr uint32_t kSelected = 0x37C;    // page +37Ch: selected record index
constexpr uint32_t kPage = 0x18;         // COAT [esp+18h]: page
constexpr uint32_t kRecordIndex = 0x38;  // COAT [esp+38h]: record index
constexpr double kHiddenRow = 100;       // rows from here on are below the panel

// One window per list page: in co-op each player has a page of their own, so the last page built must not take over
// the other one's scrolling.
struct State {
    rs7arr::PageStates states;
    bool rebuilding = false;
    bool focusHooked = false;
    bool lifetimeHooked = false;
};

State g_coop;

// The state of `page`, or null when it has none (a page the game built before this module saw it).
PageState* Find(uint32_t page) {
    return g_coop.states.Find(page);
}

uint32_t __fastcall Destroy(uint32_t page, void*, uint32_t flags) {
    using Destructor = uint32_t(__thiscall*)(uint32_t, uint32_t);

    g_coop.states.Retire(page);

    return reinterpret_cast<Destructor>(uintptr_t(kDestroy))(page, flags);
}

uint32_t Count(uint32_t page) {
    return RecordCount(page + kRecords);
}

bool Windowed(uint32_t count) {
    return count > rs7arr::kCoopRows;
}

// Re-runs the builder with the window at st.offset. The page keeps its focus lists (SwapFocusLists).
void Rebuild(PageState& st) {
    using Build = void(__thiscall*)(uint32_t);

    ClearRecords(st.page + kRecords);
    SwapFocusLists(st.page, st.spareFocus);
    g_coop.rebuilding = true;
    reinterpret_cast<Build>(uintptr_t(Dword(Dword(st.page) + kBuild)))(st.page);
    g_coop.rebuilding = false;
    SwapFocusLists(st.page, st.spareFocus);
}

// The position of record `record`'s entry in the page's focus id list, or -1.
int ListPosition(uint32_t page, uint32_t record) {
    const uint32_t begin = Dword(page + kFocusIds);
    const uint32_t end = Dword(page + kFocusIds + 4);
    char id[64];

    for (uint32_t s = begin; s < end; s += kStringSize) {
        ReadString(s, id, sizeof id);
        if (rs7arr::CoopFocusIndex(id) == int(record)) {
            return int((s - begin) / kStringSize);
        }
    }

    return -1;
}

// A focus step of `steps` records (negative: up). The stock steps move through the focus id list, which the movie
// orders by SortOrder at page entry: in record order only while the window is at the top (CONFIRMED in game). So the
// step goes by record: scroll the window onto the target record (rebuild), then give the stock step the distance
// between the two entries in the list.
void Step(uint32_t page, int steps) {
    char focus[64];

    ReadFocus(page, focus, sizeof focus);

    const int index = rs7arr::CoopFocusIndex(focus);
    const uint32_t count = Count(page);
    PageState* st = Find(page);
    int moves = steps;

    if (st && Windowed(count) && index >= 0 && uint32_t(index) < count) {
        const uint32_t target = rs7arr::CoopStep(uint32_t(index), count, steps);
        const uint32_t offset = rs7arr::CoopWindowFor(st->offset, count, target);
        const int from = ListPosition(page, uint32_t(index));
        const int to = ListPosition(page, target);

        if (offset != st->offset) {
            Rs7Log("arrangements: co-op list %08X scrolled to %u of %u", page, offset, count);
            st->offset = offset;
            Rebuild(*st);
        }

        if (from >= 0 && to >= 0) {
            moves = to - from;
        }
    }

    if (moves > 0) {
        reinterpret_cast<FocusStep>(uintptr_t(kStockFocusDown))(page, moves);
    } else if (moves < 0) {
        reinterpret_cast<FocusStep>(uintptr_t(kStockFocusUp))(page, -moves);
    }
}

void __fastcall FocusUp(uint32_t page, void*, int steps) {
    Step(page, -steps);
}

void __fastcall FocusDown(uint32_t page, void*, int steps) {
    Step(page, steps);
}

void HookFocus() {
    if (!g_coop.lifetimeHooked) {
        g_coop.lifetimeHooked =
            Dword(kPageVtable) == kDestroy && Rs7_WritePtrAtomic(kPageVtable, uintptr_t(&Destroy), nullptr);
    }

    if (g_coop.focusHooked) {
        return;
    }

    g_coop.focusHooked = true;
    if (!HookFocusSteps(kPageVtable, uintptr_t(&FocusUp), uintptr_t(&FocusDown))) {
        Rs7Log("arrangements: co-op focus steps not hooked (vtable +DCh %08X, +E0h %08X)",
               Dword(kPageVtable + kFocusUp), Dword(kPageVtable + kFocusDown));
    }
}
}  // namespace

// COAT - 0x64637C, after the FILD of the entry's SortOrder (the record index): a build the game starts itself (page
// entry) places the window over the selected record; records in the window get their row, the others a row below
// the panel.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_COAT(Context& c) {
    const uint32_t page = Dword(c.esp + kPage);
    const uint32_t index = Dword(c.esp + kRecordIndex);
    const uint32_t count = Count(page);

    HookFocus();

    if (index == 0 && !g_coop.rebuilding && g_coop.lifetimeHooked) {
        if (PageState* st = g_coop.states.Claim(page)) {
            st->offset = rs7arr::CoopWindowFor(0, count, Dword(page + kSelected));
        }
    }

    const PageState* st = Find(page);

    if (!st || !Windowed(count)) {
        return;
    }

    const double row = rs7arr::CoopInWindow(index, count, st->offset) ? double(index - st->offset) : kHiddenRow + index;

    if (!c.SetSt0(row)) {
        Rs7Log("arrangements: co-op SortOrder of record %u not set (no ST0)", index);
    }
}

// COAS - 0x6474DF, the select handler's store of the selected record: the record named by the focused entry.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_COAS(Context& c) {
    char focus[64];

    ReadFocus(c.ebx, focus, sizeof focus);

    const int index = rs7arr::CoopFocusIndex(focus);

    if (index >= 0 && uint32_t(index) < Count(c.ebx) && uint32_t(index) != c.ecx) {
        Rs7Log("arrangements: co-op select %s -> record %d (list position gave %u)", focus, index, c.ecx);
        c.ecx = uint32_t(index);
    }
}
