// Mid-hook callbacks of the per-guitar 7-string look (string7.h): the string-line and ghost-string pool request counts
// of Rnd_Guitar_LoadDependencies and string 6 in the SNG section mask (N12 / N13). They replaced hand-written naked
// stubs (docs/ARCHITECTURE.md, "Mid-hook conversion"); equivalence against the frozen stubs:
// tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "features/diag/diag.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "model/instrument.h"
#include "platform/midhook.h"

extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_N1 = 0, g_rsxr_resume_N21 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_N12 = 0;

using RsxrMid::Dword;
using RsxrMid::Push;

// ---- pool request counts (Rnd_Guitar_LoadDependencies) ----
// N1 0x5965CA `movzx eax,[eax+38h]; push ecx; push 1` (EAX = profile, the type-4 string-line request
// count) and N21 0x5965E9 `movzx ecx,[ecx+38h]; push eax; push 1` (the type-2 ghost-string count). A
// count of 6 or 7 becomes 8 with the native_publish ports (RefreshCounts preloads all 8 GuitarStrings
// slots, one instance per loaded string); otherwise 6 becomes 7 while [neck] string7_line is set; bass
// (4) and everything else stay. The stub did the movzx and both pushes itself, so the callback skips the
// replay. Flags are dead (`xor dl,dl` follows); the old stub left its own comparison flags.
namespace {
uint32_t RequestCount(uint32_t count) {
    if (count < 6 || count > 7) {
        return count;
    }
    if (g_rsxr_publish_ports) {
        return 8;
    }

    return count == 6 && g_rsxr_s7_on ? 7 : count;
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N1(RsxrMid::Context& c) {
    c.eax = RequestCount(*reinterpret_cast<const uint8_t*>(c.eax + 0x38));
    Push(c, c.ecx);
    Push(c, 1);
    c.jump = uint32_t(g_rsxr_resume_N1);
}

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N21(RsxrMid::Context& c) {
    c.ecx = RequestCount(*reinterpret_cast<const uint8_t*>(c.ecx + 0x38));
    Push(c, c.eax);
    Push(c, 1);
    c.jump = uint32_t(g_rsxr_resume_N21);
}

// ---- 7th string highlight: the stock section dimming (docs/ARCHITECTURE.md, "Highlight of the 7th string") ----
// Song load (SNG PostProcessNotes), full mode. Stock songs have no string-6 notes, so their masks are unchanged.
namespace {
constexpr uint8_t kString6 = 6;
}  // namespace

// N12 0x55CC71, single-note branch (CL = the note's string): the replayed `cmp cl,6; jae skip` keeps string 6 out of
// Section.stringMask[level]. With [neck] string7_line on, string 6 skips the replay and goes on to the stock write (its
// own level < 32 guard follows, then `or [mask], 1 << 6`). Flags: the old stub left those of its `cmp
// [g_rsxr_s7_on],0`, the hook the incoming ones; both continuations rewrite them (test_midhook_cold.py, "free").
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N12(RsxrMid::Context& c) {
    const uint8_t s = uint8_t(c.ecx);

    if ((s == kString6 || s == kString6 + 1) && g_rsxr_s7_on) {  // string 7: bit 7 (eight strings)
        c.jump = uint32_t(g_rsxr_resume_N12);
    }
}

// N13 0x55CE3A, the bound `cmp cl,6; jb loop` of the back-fill loop over strings 0-5 (CL = string, EDI = level, ESI =
// the SNG song; per level the back-fill lights a string one section early). Strings 0-5 loop on (replay). After string
// 5, Rsxr_S7MaskBackfill does the same for bit 6 of this level before the replay falls through to the stock
// continuation (the stock loop body cannot be reused for string 6: its `cl >= 6` paths skip the level guard and the
// OR). Flags: the old stub left the helper's, the hook those of the replayed cmp; `sub` rewrites them ("free").
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N13(RsxrMid::Context& c) {
    if (uint8_t(c.ecx) >= kString6) {
        Rsxr_S7MaskBackfill(reinterpret_cast<uint8_t*>(c.esi), c.edi);
    }
}

// ---- per-frame driver and the string-6 bend targets (issue #16) ----
// Cost budget (ARCHITECTURE.md section 14): N5 runs once per guitar per frame; N18 once per frame for each note
// in progress with a bend (at most one per string), so full mode costs well under the budget. N19 / N20 call only
// the integer-only Rsxr_S7BendObject and are light. At every site the x87 stack is empty or its live slots are left
// alone (full mode restores them, light mode does not touch them).
extern "C" __declspec(dllexport) uintptr_t g_rsxr_resume_N18 = 0, g_rsxr_resume_N19 = 0, g_rsxr_resume_N20 = 0;
// The sites' stock `jae` targets (bound from sites.json): where a string 6/7 the stock body cannot address goes.
extern "C" __declspec(dllexport) uintptr_t g_rsxr_target_N18 = 0, g_rsxr_target_N19 = 0, g_rsxr_target_N20 = 0;

namespace {
constexpr uint8_t kNoFret = 0xFF;
constexpr uint32_t kBend = 0;  // the Rsxr_S7BendTarget kind (string7.h)

constexpr uint8_t kStockSlots = 6;  // the stock GuitarStrings arrays (+4 main, +1Ch ghost) hold six strings

uint8_t StockCount(uint32_t info) {
    return *reinterpret_cast<const uint8_t*>(info + rsxr::kInfoStrings);
}

// The stock bound `cmp <string>,[info+0Bh]; jae` lets the string through and the stock body can address it: the
// replay takes the stock path.
bool InStockRange(uint8_t string, uint32_t info) {
    return string < kStockSlots && string < StockCount(info);
}

// A string the stock bound lets through but the stock arrays do not hold: with C-lite, +0Bh is the build count (7 or
// 8), so the stock body would index string 6/7 past the six main slots (into the ghosts). It must go to the stock
// target instead (a native path that applies has already joined). Counts up to 6 never get here: the replay rejects.
bool PastStockSlots(uint8_t string, uint32_t info) {
    return string >= kStockSlots && string < StockCount(info);
}

// The native object of added string 6 or 7 of GuitarStrings `gs`, nullptr without one. String 6
// goes through Rsxr_S7BendTarget / Rsxr_S7BendObject (their logs, the emulator oracle); string 7 (eight strings) asks
// the native strings directly.
uint8_t* AddedString(uint32_t gs, uint8_t s) {
    if (s != 7 || !gs) {
        return nullptr;
    }

    return RsxrGs_Active(reinterpret_cast<uint8_t*>(uintptr_t(gs)), 7, false);
}

// The added-string path of N18 (AL = note string, ESI = guitar info, ECX = GuitarStrings, DL = note fret): for
// string 6 or 7 with a fret, its native object of `kind` gets ECX, EDX = the string and the stock body is skipped to
// `join` after the site (the stock code there reads only ECX). EAX = the join address, as the old stubs left it (they
// jumped through EAX; the stock code after the join reloads EAX before reading it). A string 6/7 past the stock slots
// without that path: `reject` (the stock target). Anything else: the replay `cmp al,[esi+0Bh]; jae` (strings in range:
// the stock body, else the stock target).
void BendAdded(RsxrMid::Context& c, uint32_t amountAt, uint32_t join, uintptr_t reject) {
    const uint8_t s = uint8_t(c.eax);

    if (InStockRange(s, c.esi)) {
        return;
    }

    const bool fret = uint8_t(c.edx) != kNoFret;
    uint8_t* const obj = !fret ? nullptr
                         : s == 6
                             ? Rsxr_S7BendTarget(reinterpret_cast<void*>(c.ecx), kBend, uint8_t(c.edx), Dword(amountAt))
                             : AddedString(c.ecx, s);

    if (!obj) {
        if (PastStockSlots(s, c.esi)) {
            c.jump = uint32_t(reject);
        }
        return;
    }

    c.ecx = uint32_t(reinterpret_cast<uintptr_t>(obj));
    c.edx = s;
    c.jump = join;
    c.eax = join;
}

// The added-string path of N19 / N20 (a release): joins at `join` with the native string 6 or 7 of
// `gs` in `reg`. A string 6/7 past the stock slots without one: `reject`. Anything else: the replay (`string` in the
// stock range: the stock body, else the stock target).
void ReleaseAdded(RsxrMid::Context& c, uint8_t string, uint32_t info, uint32_t gs, uint32_t& reg, uint32_t join,
                  uintptr_t reject) {
    if (InStockRange(string, info)) {
        return;
    }

    const uint32_t obj = uint32_t(reinterpret_cast<uintptr_t>(
        string == 6 ? Rsxr_S7BendObject(reinterpret_cast<void*>(gs), 0) : AddedString(gs, string)));

    if (obj) {
        reg = obj;
        c.jump = join;
    } else if (PastStockSlots(string, info)) {
        c.jump = uint32_t(reject);
    }
}
}  // namespace

// N5 0x592AD4 in Rnd_Guitar_Update (EBX = Rnd_Guitar; also the target of the `je` when GuitarStrings is null):
// Rsxr_S7Update, then the replay `mov eax,[ebx+18h]; test eax,eax` (its flags feed the `je` after it). Full mode: the
// per-frame drivers use x87/SSE.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N5(RsxrMid::Context& c) {
    if (g_rsxr_diag) {
        RsxrDiag_FlushNoteHeads();  // full mode every frame, even when diagnostics started off
    }
    const RsxrMid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rsxr_S7Update(reinterpret_cast<uint8_t*>(c.ebx));
}

// N18 0x7E3FFA in the note-in-progress update 0x7E3700, single-note bend `cmp al,[esi+0Bh]; jae` ([EBP-64h] = the
// amount, log only): string 6/7 joins at the stock `cmp [ecx+4],0` (resume + 10h), so the stock
// Rnd_GuitarString_SetBend call plays its `bendup_NN` sequence on the native string 6/7. Flags are dead at every
// continuation.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N18(RsxrMid::Context& c) {
    BendAdded(c, c.ebp - 0x64, uint32_t(g_rsxr_resume_N18) + 0x10, g_rsxr_target_N18);
}

// N19 0x7E4059 (bend release, no bend at this time) and N20 0x7E43DB (release of a bend note when it ends, ST0 live):
// `cmp al,[edx+0Bh]; jae` (AL = note string, EDX = guitar info, ECX = GuitarStrings). String 6/7 with a native
// object: EAX = it, join at the stock `cmp byte [eax+0BDh],0FFh` (resume + 7). Light; flags are dead.
extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N19(RsxrMid::Context& c) {
    ReleaseAdded(c, uint8_t(c.eax), c.edx, c.ecx, c.eax, uint32_t(g_rsxr_resume_N19) + 7, g_rsxr_target_N19);
}

extern "C" __declspec(dllexport) void __cdecl Rsxr_Mid_N20(RsxrMid::Context& c) {
    ReleaseAdded(c, uint8_t(c.eax), c.edx, c.ecx, c.eax, uint32_t(g_rsxr_resume_N20) + 7, g_rsxr_target_N20);
}
