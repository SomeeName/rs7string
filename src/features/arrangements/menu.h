#pragma once
#include <cstdint>

// Helpers shared by the arrangement menu hooks (hooks.cpp: the Learn a Song page, coop.cpp: the co-op arrangement
// list). Both pages derive from one menu class: view definition root +1A0h, page node +1C4h, focus lists +214h /
// +220h, focused id +1FCh, vtable +B0h builder and +DCh / +E0h focus steps.
namespace rsxrmenu {
constexpr uint32_t kRecordSize = 0xFC;        // an arrangement record (filled by 0x625E70)
constexpr uint32_t kRoot = 0x1A0;             // page +1A0h: the view definition root, a new one per build (0x5C6C40)
constexpr uint32_t kPageNode = 0x1C4;         // page +1C4h: the page's node under the root
constexpr uint32_t kFocus = 0x1FC;            // page +1FCh: the focused button's id, a game string
constexpr uint32_t kBuild = 0xB0;             // page vtable +B0h: the page builder (thiscall, no arguments)
constexpr uint32_t kFocusUp = 0xDC;           // page vtable +DCh: 0x5DA290 (thiscall; steps), arrow Up and the wheel
constexpr uint32_t kFocusDown = 0xE0;         // page vtable +E0h: 0x5DA370, arrow Down and the wheel
constexpr uint32_t kStockFocusUp = 0x5DA290;  // both stop at the ends
constexpr uint32_t kStockFocusDown = 0x5DA370;
constexpr uint32_t kRemoveMember = 0x881500;  // stdcall (node, key*)
constexpr uint32_t kMakeKey = 0x87C470;       // fastcall (text, -, out key*)
constexpr uint32_t kMerge = 0x880BC0;  // merge a node's members into another (thiscall this = target; source, replace)

using FocusStep = void(__thiscall*)(uint32_t, int);

uint32_t& Field(uint32_t address);

// Number of records in the vector at `vector` (begin, end).
uint32_t RecordCount(uint32_t vector);

// The builders append the page's records (0x625E70): empty the vector first, as on page entry.
void ClearRecords(uint32_t vector);

// Calls the node's reference method at vtable `offset` (0 add, 4 release); nothing for 0.
void RefOp(uint32_t node, uint32_t offset);

constexpr uint32_t kStringSize = 0x18;   // a game string, the element of the focus lists
constexpr uint32_t kFocusLists = 0x214;  // page +214h / +220h: focus keys / ids, vectors (begin, end, capacity)
constexpr uint32_t kFocusIds = 0x220;    // the list the stock focus steps move through (by position)
constexpr uint32_t kFocusListDwords = 6;

// The game string at `address` into `out`; "" if unreadable or too long.
void ReadString(uint32_t address, char* out, uint32_t n);

// The page's focused button id (+1FCh) into `out`; "" if unreadable or too long.
void ReadFocus(uint32_t page, char* out, uint32_t n);

// A builder refills the page's focus lists from its nodes ("arrangement3", "playSong"), while the lists in use hold
// the movie's ids from page entry ("Buttons.arrangement3.States"), which the focus steps and the select handlers look
// the focus up in: around a rebuild the builder gets the `spare` pair (cleared and refilled with its own allocator,
// kept for the next rebuild), the page keeps its own. Call it before and after the build.
void SwapFocusLists(uint32_t page, uint32_t (&spare)[kFocusListDwords]);

// Points the focus steps of the page vtable `vtable` at `up` / `down`, if they still hold the stock steps.
bool HookFocusSteps(uint32_t vtable, uintptr_t up, uintptr_t down);
}  // namespace rsxrmenu
