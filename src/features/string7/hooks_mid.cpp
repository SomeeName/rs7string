// Mid-hook callbacks of the per-guitar 7-string look (string7.h): the string-line and ghost-string pool request counts
// of Rnd_Guitar_LoadDependencies and string 6 in the SNG section mask (N12 / N13). They replaced hand-written naked
// stubs (docs/ARCHITECTURE.md, "Mid-hook conversion"); equivalence against the frozen stubs:
// tools/tests/test_midhook_cold.py. Callbacks must not throw.
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "platform/midhook.h"

extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_N1 = 0, g_rs7_resume_N21 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_N12 = 0;

using Rs7Mid::Dword;
using Rs7Mid::Push;

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
    if (g_rs7_publish_ports) {
        return 8;
    }

    return count == 6 && g_rs7_s7_on ? 7 : count;
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N1(Rs7Mid::Context& c) {
    c.eax = RequestCount(*reinterpret_cast<const uint8_t*>(c.eax + 0x38));
    Push(c, c.ecx);
    Push(c, 1);
    c.jump = uint32_t(g_rs7_resume_N1);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N21(Rs7Mid::Context& c) {
    c.ecx = RequestCount(*reinterpret_cast<const uint8_t*>(c.ecx + 0x38));
    Push(c, c.eax);
    Push(c, 1);
    c.jump = uint32_t(g_rs7_resume_N21);
}

// ---- 7th string highlight: the stock section dimming (docs/ARCHITECTURE.md, "Highlight of the 7th string") ----
// Song load (SNG PostProcessNotes), full mode. Stock songs have no string-6 notes, so their masks are unchanged.
namespace {
constexpr uint8_t kString6 = 6;
}  // namespace

// N12 0x55CC71, single-note branch (CL = the note's string): the replayed `cmp cl,6; jae skip` keeps string 6 out of
// Section.stringMask[level]. With [neck] string7_line on, string 6 skips the replay and goes on to the stock write (its
// own level < 32 guard follows, then `or [mask], 1 << 6`). Flags: the old stub left those of its `cmp
// [g_rs7_s7_on],0`, the hook the incoming ones; both continuations rewrite them (test_midhook_cold.py, "free").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N12(Rs7Mid::Context& c) {
    if (uint8_t(c.ecx) == kString6 && g_rs7_s7_on) {
        c.jump = uint32_t(g_rs7_resume_N12);
    }
}

// N13 0x55CE3A, the bound `cmp cl,6; jb loop` of the back-fill loop over strings 0-5 (CL = string, EDI = level, ESI =
// the SNG song; per level the back-fill lights a string one section early). Strings 0-5 loop on (replay). After string
// 5, Rs7_S7MaskBackfill does the same for bit 6 of this level before the replay falls through to the stock
// continuation (the stock loop body cannot be reused for string 6: its `cl >= 6` paths skip the level guard and the
// OR). Flags: the old stub left the helper's, the hook those of the replayed cmp; `sub` rewrites them ("free").
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N13(Rs7Mid::Context& c) {
    if (uint8_t(c.ecx) >= kString6) {
        Rs7_S7MaskBackfill(reinterpret_cast<uint8_t*>(c.esi), c.edi);
    }
}

// ---- per-frame driver and the string-6 bend targets (issue #16) ----
// Cost budget (ARCHITECTURE.md section 15): N5 runs once per guitar per frame; N18 / N22 once per frame for each note
// in progress with a bend (at most one per string), so full mode costs well under the budget. N19 / N20 / N23 call only
// the integer-only Rs7_S7BendObject and are light. At every site the x87 stack is empty or its live slots are left
// alone (full mode restores them, light mode does not touch them).
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_N18 = 0, g_rs7_resume_N19 = 0, g_rs7_resume_N20 = 0;
extern "C" __declspec(dllexport) uintptr_t g_rs7_resume_N22 = 0, g_rs7_resume_N23 = 0;

namespace {
constexpr uint8_t kNoFret = 0xFF;
constexpr uint32_t kBend = 0, kGhostBend = 2;  // Rs7_S7BendTarget kinds (string7.h)

// The stock bound `cmp <string>,[info+0Bh]; jae` lets the string through: the replay takes the stock path.
bool InStockRange(uint8_t string, uint32_t info) {
    return string < *reinterpret_cast<const uint8_t*>(info + 0x0B);
}

// The string-6 path of N18 / N22 (AL = note string, ESI = guitar info, ECX = GuitarStrings, DL = note fret): for string
// 6 with a fret, the native string-6 object of `kind` gets ECX, EDX = 6 and the stock body is skipped to `join` after
// the site. EAX = the join address, as the old stubs left it (they jumped through EAX; the stock code after the join
// reloads EAX before reading it). Anything else: the replay `cmp al,[esi+0Bh]; jae` (strings in range: the stock body,
// else the stock target).
void BendString6(Rs7Mid::Context& c, uint32_t kind, uint32_t amountAt, uint32_t join) {
    if (InStockRange(uint8_t(c.eax), c.esi) || uint8_t(c.eax) != 6 || uint8_t(c.edx) == kNoFret) {
        return;
    }

    uint8_t* const obj = Rs7_S7BendTarget(reinterpret_cast<void*>(c.ecx), kind, uint8_t(c.edx), Dword(amountAt));
    if (!obj) {
        return;
    }

    c.ecx = uint32_t(reinterpret_cast<uintptr_t>(obj));
    c.edx = 6;
    c.jump = join;
    c.eax = join;
}

// The string-6 path of N19 / N20 / N23 (a release): the native string 6 (or its ghost) of `gs`, or 0 when the replay
// must run (`string` in the stock range: the stock body; another string or no native object: the stock target).
uint32_t ReleaseString6(uint8_t string, uint32_t info, uint32_t gs, bool ghost) {
    if (InStockRange(string, info) || string != 6) {
        return 0;
    }

    return uint32_t(reinterpret_cast<uintptr_t>(Rs7_S7BendObject(reinterpret_cast<void*>(gs), ghost ? 1 : 0)));
}
}  // namespace

// N5 0x592AD4 in Rnd_Guitar_Update (EBX = Rnd_Guitar; also the target of the `je` when GuitarStrings is null):
// Rs7_S7Update, then the replay `mov eax,[ebx+18h]; test eax,eax` (its flags feed the `je` after it). Full mode: the
// per-frame drivers use x87/SSE.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N5(Rs7Mid::Context& c) {
    const Rs7Mid::GameFcw fcw(c);  // game code runs below: the game's x87 control word

    Rs7_S7Update(reinterpret_cast<uint8_t*>(c.ebx));
}

// N18 0x7E3FFA in the note-in-progress update 0x7E3700, single-note bend `cmp al,[esi+0Bh]; jae` ([EBP-64h] = the
// amount, log only): string 6 joins at the stock `cmp [ecx+4],0` (resume + 10h), so the stock Rnd_GuitarString_SetBend
// call plays its `bendup_NN` sequence on the native string 6. Flags are dead at every continuation.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N18(Rs7Mid::Context& c) {
    BendString6(c, kBend, c.ebp - 0x64, uint32_t(g_rs7_resume_N18) + 0x10);
}

// N22 0x7E3ED5, the chart bend on the ghost strings (stock body `mov ecx,[ecx+edx*4+1Ch]`, [EBP-60h] = the amount
// before *2/3): the ghost of string 6 joins at `cmp [ecx+4],0` (resume + 0Ch), "ghost_up_NN" on the ghost.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N22(Rs7Mid::Context& c) {
    BendString6(c, kGhostBend, c.ebp - 0x60, uint32_t(g_rs7_resume_N22) + 0x0C);
}

// N19 0x7E4059 (bend release, no bend at this time) and N20 0x7E43DB (release of a bend note when it ends, ST0 live):
// `cmp al,[edx+0Bh]; jae` (AL = note string, EDX = guitar info, ECX = GuitarStrings). String 6 with a native string-6
// object: EAX = it, join at the stock `cmp byte [eax+0BDh],0FFh` (resume + 7). Light; flags are dead.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N19(Rs7Mid::Context& c) {
    if (const uint32_t string6 = ReleaseString6(uint8_t(c.eax), c.edx, c.ecx, false)) {
        c.eax = string6;
        c.jump = uint32_t(g_rs7_resume_N19) + 7;
    }
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N20(Rs7Mid::Context& c) {
    if (const uint32_t string6 = ReleaseString6(uint8_t(c.eax), c.edx, c.ecx, false)) {
        c.eax = string6;
        c.jump = uint32_t(g_rs7_resume_N20) + 7;
    }
}

// N23 0x7E4347 in Rnd_Note_UpdateEnd 0x7E4230, the ghost release of an ending note: `cmp cl,[esi+0Bh]; jae` (CL =
// string, EDX = GuitarStrings; two x87 slots live). The ghost of string 6: ECX = it, join at `cmp byte [ecx+0BDh],0FFh`
// (resume + 7). Light; flags are dead.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_N23(Rs7Mid::Context& c) {
    if (const uint32_t ghost6 = ReleaseString6(uint8_t(c.ecx), c.esi, c.edx, true)) {
        c.ecx = ghost6;
        c.jump = uint32_t(g_rs7_resume_N23) + 7;
    }
}
