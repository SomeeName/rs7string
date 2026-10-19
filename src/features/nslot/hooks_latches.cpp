// Loop latches of the per-string technique checks (tracker.h): the stock body runs for strings 0-5, then strings 6/7
// run the same test on the eval's N-slot tails (the stock body would reject them with its string < 6 guard). Each
// site replaced `inc <counter>; add <pointer>,<step>; cmp <counter>,6; jb <loop>`; the flags at both exits are that
// compare's (exact). At the exit the pointer is eval + <base> + step * 6.
//
// Converted from naked stubs (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_nslot.py). Light mode
// (every tracker update, integer only, LIGHT_CALLBACKS in tools/tests/emu_harness.py).
#include "features/nslot/nslot.h"
#include "features/nslot/tracker.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_UP1 = 0, g_rs7_target_UP1 = 0, g_rs7_resume_UP2 = 0;
__declspec(dllexport) uintptr_t g_rs7_target_UP2 = 0, g_rs7_resume_UP3 = 0, g_rs7_target_UP3 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PD2 = 0, g_rs7_target_PD2 = 0, g_rs7_resume_PD3 = 0;
__declspec(dllexport) uintptr_t g_rs7_target_PD3 = 0, g_rs7_resume_PD4 = 0, g_rs7_target_PD4 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_PD5 = 0, g_rs7_target_PD5 = 0;
}

namespace {
using Reg = uint32_t Context::*;

constexpr uint32_t kStrings = 6;
constexpr uint32_t kFieldBase = 0x2BC;  // UP / PD2-PD4: the eval's int32 per-string fields
constexpr uint32_t kByteBase = 0xBEC;   // PD5: the eval's int8 per-string fields

uint8_t Low(uint32_t r) {
    return static_cast<uint8_t>(r);
}

void SetLow(uint32_t& r, uint8_t value) {
    r = (r & 0xFFFFFF00u) | value;
}

// The replaced loop step. False: strings 0-5 are not done, the hook continues at the loop (jump = target). True:
// returns the eval (pointer - base - step * 6); the caller does the tails and continues at resume.
bool Step(Context& c, Reg counter, Reg pointer, uint32_t step, uint32_t base, uintptr_t resume, uintptr_t target,
          const uint8_t** eval) {
    const uint8_t count = Low(c.*counter) + 1;

    SetLow(c.*counter, count);
    c.*pointer += step;
    c.Compare(count, kStrings, 8);

    if (count < kStrings) {
        c.jump = static_cast<uint32_t>(target);
        return false;
    }

    *eval = reinterpret_cast<const uint8_t*>(c.*pointer - base - step * kStrings);
    c.jump = static_cast<uint32_t>(resume);

    return true;
}

// UP1-UP3: counter AL, accumulator (low byte) cleared when a sounding tail string is pitched.
void AllUnpitched(Context& c, Reg pointer, Reg accumulator, uintptr_t resume, uintptr_t target) {
    const uint8_t* eval;

    if (!Step(c, &Context::eax, pointer, 4, kFieldBase, resume, target, &eval)) {
        return;
    }

    if (rs7track::TailPitched(eval)) {
        SetLow(c.*accumulator, 0);
    }
}

// PD2-PD4: pointer ESI, result AL, scratch (low byte) = the last SETcc.
void FoldLatch(Context& c, Reg counter, Reg scratch, uint32_t bits, bool byteMask, bool none, uintptr_t resume,
               uintptr_t target) {
    const uint8_t* eval;

    if (!Step(c, counter, &Context::esi, 4, kFieldBase, resume, target, &eval)) {
        return;
    }

    uint8_t last = Low(c.*scratch);

    SetLow(c.eax, rs7track::FoldTails(eval, bits, byteMask, none, Low(c.eax), &last));
    SetLow(c.*scratch, last);
}
}  // namespace

// UP1 - 0x87617D in Play_Tracker_Update 0x8760D0 (pointer ESI, accumulator CL); UP2 - 0x8769CB in 0x876980 (ESI, DL);
// UP3 - 0x876C6C in 0x876C20 (EDI, DL).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_UP1(Context& c) {
    AllUnpitched(c, &Context::esi, &Context::ecx, g_rs7_resume_UP1, g_rs7_target_UP1);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_UP2(Context& c) {
    AllUnpitched(c, &Context::esi, &Context::edx, g_rs7_resume_UP2, g_rs7_target_UP2);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_UP3(Context& c) {
    AllUnpitched(c, &Context::edi, &Context::edx, g_rs7_resume_UP3, g_rs7_target_UP3);
}

// PD2 - 0x877D4A, latch of 0x877D20 (counter CL, scratch DL): all sounding strings slide (800h).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PD2(Context& c) {
    FoldLatch(c, &Context::ecx, &Context::edx, 0x800, false, false, g_rs7_resume_PD2, g_rs7_target_PD2);
}

// PD3 - 0x877DB6, latch of 0x877D60 (BL, CL): no sounding string bends / slides / unpitched slides / tremolos
// (1000h|800h|400000h|10h).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PD3(Context& c) {
    FoldLatch(c, &Context::ebx, &Context::ecx, 0x401810, false, true, g_rs7_resume_PD3, g_rs7_target_PD3);
}

// PD4 - 0x877DF7, latch of 0x877DD0 (CL, DL): all sounding strings tremolo (byte mask 10h).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PD4(Context& c) {
    FoldLatch(c, &Context::ecx, &Context::edx, 0x10, true, false, g_rs7_resume_PD4, g_rs7_target_PD4);
}

// PD5 - 0x877E56, latch of 0x877E10: the extreme slide distance (counter BL, pointer ESI = eval + 0BECh + string,
// step 1, result EAX). EDX = string 7's fret and ECX = the last fretted tail's distance, as the old stub left them.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_PD5(Context& c) {
    const uint8_t* eval;

    if (!Step(c, &Context::ebx, &Context::esi, 1, kByteBase, g_rs7_resume_PD5, g_rs7_target_PD5, &eval)) {
        return;
    }

    int32_t distance = static_cast<int32_t>(c.ecx);

    c.eax = static_cast<uint32_t>(rs7track::ExtremeSlide(eval, static_cast<int32_t>(c.eax), &distance));
    c.ecx = static_cast<uint32_t>(distance);
    c.edx = eval[rs7ns::kEvFret67 + 1];
}
