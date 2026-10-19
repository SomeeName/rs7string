// R3a/R3b/R3c (NoteFx position, three sites in Rnd_NoteFx_Init 0x40CAA0) and R4 (note feedback position, in
// Rnd_RenderNote_GetLanePos 0x7A6FE0): the displaced code reads a lane cell's vec3 position, `lea eax,[ecx+eax+3Ch];
// fld [eax]` (R4 with a `pop esi` between them), ECX = string * 4Ch. Strings 0-5 replay it. String 6 has no stock cell:
// its position is extrapolated from strings 0 and 1, 2 * cell0 - cell1, into a scratch vec3, and the callback does
// what the replay would have done with the scratch: EAX = &scratch, ST0 = scratch.x (Context::PushSt0), R4 also
// `pop esi`.
//
// Full mode (x87 result, float math). The cost budget allows it: Rnd_NoteFx_Init runs from Rnd_NoteFx_Spawn and
// GetLanePos from Rnd_NoteFeedback_Spawn (CONFIRMED, call graph), so these run once per spawned effect, not per frame
// (HYPOTHESIS, hit counts not measured in game). R2 (note head z) and R6 (lane offset, Rnd_GemLaneOffset_Update) run
// per note and frame: sse mode (below), the x87 result through Rs7Mid::LoadSt0.
//
// The old stubs computed 2 * a - b on the x87 stack and stored a float; Rs7S7_ExtrapolateLane (string7.cpp) gives the
// same float for finite results. The sticky x87 exception flags (precision) the old arithmetic raised are not
// reproduced (tools/tests/test_midhook_x87.py, docs/ARCHITECTURE.md section 15 "x87 ST0").
#include "features/diag/diag.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <atomic>
#include <cstdint>
#include <cstring>

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_R3a = 0, g_rs7_resume_R3b = 0, g_rs7_resume_R3c = 0, g_rs7_resume_R4 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_R2 = 0, g_rs7_resume_R6 = 0;
__declspec(dllexport) float g_rs7_scratch_R3a[3] = {};
__declspec(dllexport) float g_rs7_scratch_R3b[3] = {};
__declspec(dllexport) float g_rs7_scratch_R3c[3] = {};
__declspec(dllexport) float g_rs7_scratch_R4[3] = {};
}

// The x87 stack was full at a string-6 hit (`fld` overflows: the stock invalid-operation result): logged once.
// Not inlined: the emulator tests replace this export with `ret`.
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rs7_S7LaneStackFull() {
    static std::atomic<bool> logged{false};

    if (!logged.exchange(true)) {
        Rs7Log("lanes: R3/R4 string 6 with a full x87 stack: the replayed fld loads the extrapolated lane (once)");
    }
}

namespace {
constexpr uint32_t kRow = 0x4C;          // one string's lane cell row
constexpr uint32_t kString6 = 6 * kRow;  // ECX of string 6
constexpr uint32_t kPosition = 0x3C;     // the vec3 in a cell

float At(uint32_t address) {
    return *reinterpret_cast<const float*>(address);
}

// String 6 only: the scratch position, EAX and ST0 as after the displaced `lea; fld`, past the replay. With a full x87
// stack (PushSt0 refuses) EAX is rebased so the replayed `lea eax,[ecx+eax+3Ch]` lands on the scratch and the replay's
// own `fld` loads it, overflow included: what the old stub did. Returns true when the replay was skipped.
bool CloneLane(Rs7Mid::Context& c, float* scratch, uintptr_t resume) {
    if (c.ecx != kString6) {
        return false;
    }

    const uint32_t cell0 = c.eax + kPosition;

    for (uint32_t i = 0; i < 3; ++i) {
        scratch[i] = Rs7S7_ExtrapolateLane(At(cell0 + 4 * i), At(cell0 + kRow + 4 * i));
    }

    const uint32_t address = uint32_t(uintptr_t(scratch));

    if (!c.PushSt0(scratch[0])) {
        Rs7_S7LaneStackFull();
        c.eax = address - c.ecx - kPosition;
        return false;
    }

    c.eax = address;
    c.jump = uint32_t(resume);

    return true;
}
}  // namespace

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R3a(Rs7Mid::Context& c) {
    CloneLane(c, g_rs7_scratch_R3a, g_rs7_resume_R3a);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R3b(Rs7Mid::Context& c) {
    CloneLane(c, g_rs7_scratch_R3b, g_rs7_resume_R3b);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R3c(Rs7Mid::Context& c) {
    CloneLane(c, g_rs7_scratch_R3c, g_rs7_resume_R3c);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R4(Rs7Mid::Context& c) {
    if (CloneLane(c, g_rs7_scratch_R4, g_rs7_resume_R4)) {
        c.esi = Rs7Mid::Dword(c.esp);  // the displaced `pop esi` (a replay does its own)
        c.esp += 4;
    }
}

// ---- R2 and R6: sse mode (Rs7Mid::Mode::Sse, SSE_CALLBACKS) ----
// Both replace `add r,r/m; fld dword [cell + string * 4Ch]` for string 6 (no stock cell) with the extrapolated lane:
// the `add` with its flags (Add32, the last flag writer as in stock), then the float through LoadSt0, past the replay.
// The next game instruction stores ST0 as a float (R2 0x79E730, R6 0x7A8C30), so the x87 status flags the old stubs'
// x87 arithmetic raised are the only difference (tools/tests/test_midhook_sse_sites.py).
namespace {
uint32_t Bits(float value) {
    uint32_t bits;

    std::memcpy(&bits, &value, sizeof bits);

    return bits;
}

// |a - b| as float bits (the sign cleared: fabs without CRT / x87).
uint32_t AbsDifference(float a, float b) {
    return Bits(a - b) & 0x7fffffffu;
}
}  // namespace

// R2 - note head lane z (0x79E726): `add edx,[ebp+1Ch]; fld dword [ebx+edx+44h]`, EBX = string * 4Ch. String 6: 2 *
// cell0.z - cell1.z. D1 ([debug] diagnostics, g_rs7_diag != 0, strings 0 and 6): Rs7_DiagNoteHead gets string
// [ebp+14h], fret [ebp+18h], open [ebp+10h], the row, the floor z [ebp-30h], the lane z and |z - floor| (as the game
// computes it at 0x79E749). Strings 1-5, and string 0 without diagnostics, run the stock replay.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R2(Rs7Mid::Context& c) {
    const bool six = c.ebx == kString6;
    const bool log = g_rs7_diag && (six || c.ebx == 0);

    if (!six && !log) {
        return;
    }

    const uint32_t offset = Rs7Mid::Dword(c.ebp + 0x1C), row = c.edx + offset;
    const float z = six ? Rs7S7_ExtrapolateLane(At(row + 0x44), At(row + 0x90)) : At(row + 0x44);

    if (log) {
        Rs7_DiagNoteHead(Rs7Mid::Byte(c.ebp + 0x14), Rs7Mid::Byte(c.ebp + 0x18), Rs7Mid::Byte(c.ebp + 0x10),
                         Rs7Mid::Ptr(row), Rs7Mid::Dword(c.ebp - 0x30), Bits(z), AbsDifference(z, At(c.ebp - 0x30)));
    }

    if (Rs7Mid::LoadSt0(c, z)) {
        c.edx = c.Add32(c.edx, offset);
        c.jump = uint32_t(g_rs7_resume_R2);
    }
}

// R6 - lane offset y (0x7A8C2A, Rnd_GemLaneOffset_Update): `add eax,edi; fld dword [ecx+eax+40h]`, ECX = string * 4Ch.
// String 6: 2 * cell0.y - cell1.y; the others run the stock replay.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_R6(Rs7Mid::Context& c) {
    if (c.ecx != kString6) {
        return;
    }

    const uint32_t row = c.eax + c.edi;

    if (Rs7Mid::LoadSt0(c, Rs7S7_ExtrapolateLane(At(row + 0x40), At(row + 0x8C)))) {
        c.eax = c.Add32(c.eax, c.edi);
        c.jump = uint32_t(g_rs7_resume_R6);
    }
}
