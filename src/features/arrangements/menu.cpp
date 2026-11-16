// Helpers shared by the arrangement menu hooks (menu.h).
#include "features/arrangements/menu.h"
#include "model/gamestring.h"
#include "platform/midhook.h"
#include "platform/patch.h"
#include "platform/safecopy.h"

using Rs7Mid::Dword;

namespace rs7menu {
namespace {
constexpr uint32_t kRecordDtor = 0x4ED6A0;  // a record's destructor (thiscall, no arguments)
}  // namespace

uint32_t& Field(uint32_t address) {
    return *reinterpret_cast<uint32_t*>(uintptr_t(address));
}

uint32_t RecordCount(uint32_t vector) {
    const uint32_t begin = Dword(vector);
    const uint32_t end = Dword(vector + 4);

    return end > begin ? (end - begin) / kRecordSize : 0;  // a torn or garbage vector counts as empty
}

void ClearRecords(uint32_t vector) {
    using Destroy = void(__thiscall*)(uint32_t);

    const uint32_t begin = Field(vector);

    for (uint32_t r = Field(vector + 4); r > begin && r - begin >= kRecordSize; r -= kRecordSize) {
        reinterpret_cast<Destroy>(uintptr_t(kRecordDtor))(r - kRecordSize);
    }

    Field(vector + 4) = begin;
}

void RefOp(uint32_t node, uint32_t offset) {
    using Method = void(__thiscall*)(uint32_t);

    if (node) {
        reinterpret_cast<Method>(uintptr_t(Dword(Dword(node) + offset)))(node);
    }
}

// The game's string (read by 0x87C180): +0 data (the inline buffer itself while +14h == this + 10h), +10h end,
// +14h capacity end.
void ReadString(uint32_t address, char* out, uint32_t n) {
    rs7::ReadGameString(reinterpret_cast<const void*>(uintptr_t(address)), out, n);
}

void ReadFocus(uint32_t page, char* out, uint32_t n) {
    ReadString(page + kFocus, out, n);
}

void SwapFocusLists(uint32_t page, uint32_t (&spare)[kFocusListDwords]) {
    for (uint32_t i = 0; i < kFocusListDwords; ++i) {
        uint32_t& field = Field(page + kFocusLists + 4 * i);
        const uint32_t kept = field;

        field = spare[i];
        spare[i] = kept;
    }
}

bool HookFocusSteps(uint32_t vtable, uintptr_t up, uintptr_t down) {
    return Dword(vtable + kFocusUp) == kStockFocusUp && Dword(vtable + kFocusDown) == kStockFocusDown &&
           Rs7_WritePtrPair(vtable + kFocusUp, up, down);
}
}  // namespace rs7menu
