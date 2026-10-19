// The chord loops over eight strings ([chords] native_chordloops): the CL* jmp sites and the string objects they use.
// The stock CLB* / CLCT* / CLNF* sites are byte patches (registration.cpp); C8/T8 prefixes are unchanged, tracker child
// queries already cover eight slots.
//
//   CLBM  chord bend mask test: C8 mask[8]           CLGU  chord chart bend -> ghost string (6/7)
//   CLBA  bend data address: C8 bends[8]             CLGE  chord ghost bend release (6/7)
//   CLVB  stop-pluck path (6/7)
//
// CLGU / CLGE / CLVB: strings 0-5 do exactly what the replaced stock instructions did; strings >= 6 ask
// Rs7ChordLoops_Target for the string object (main or ghost) and join the stock code with it, or reject like a missing
// string.
//
// Light mid-hooks (per chord note in the play loops; integer only, so the live x87 bend values survive; LIGHT_CALLBACKS
// in tools/tests/emu_harness.py), converted from naked stubs (refactor step 7 batch 5, frozen at 9c0e44f,
// tools/tests/test_midhook_features.py). Every callback skips the replay and does the replaced instructions' work
// itself. CLBM reproduces its TEST flags (the SETA at the resume reads them), the stock paths of CLGU / CLGE / CLVB
// their last compare (as the old stubs did); the old stubs' string 6/7 paths and CLBA left flags that no continuation
// reads (contract "free").
#include "features/chords/chords.h"
#include "platform/midhook.h"

using Rs7Mid::Context;
using Rs7Mid::Dword;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_CLBM = 0, g_rs7_resume_CLBA = 0, g_rs7_resume_CLGU = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_CLGE = 0, g_rs7_resume_CLVB = 0;
}

namespace {
uint32_t Low8(uint32_t r) {
    return r & 0xFF;
}

// The stock loop's `cmp string,[info+0Bh]; jae reject`: whether it accepts string s (below the InstrumentInfo's string
// count), with the compare's flags, which the stock instructions after the site may still hold.
bool InStockCount(Context& c, uint32_t s, uint32_t info) {
    const uint32_t count = Rs7Mid::Byte(info + 0x0B);

    c.Compare(s, count, 8);

    return s < count;
}

// The native main (ghost = 0) or ghost (1) string object of string 6/7, 0 when there is none.
uint32_t NativeString(uint32_t owner, uint32_t s, uint32_t ghost) {
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(Rs7ChordLoops_Target(reinterpret_cast<uint8_t*>(owner), s, ghost)));
}

uint32_t Resume(uintptr_t resume, uint32_t offset = 0) {
    return static_cast<uint32_t>(resume) + offset;
}
}  // namespace

// CLBM - 0x7A62D4: `test dword [edx+eax*4+948h],1000h` over the C8 mask[8]; the SETA CL at the resume reads its flags.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CLBM(Context& c) {
    c.Test(Dword(c.edx + c.eax * 4 + 0x948), 0x1000, 32);
    c.jump = Resume(g_rs7_resume_CLBM);
}

// CLBA - 0x7E3C62: `imul eax,eax,184h; lea eax,[eax+ecx+18h]` -> the C8 bends[8] base +988h (BendData32 stride
// unchanged). The following 0x7E3C6C is an inbound branch target and is not part of the site.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CLBA(Context& c) {
    c.eax = c.eax * 0x184 + c.ecx + 0x988;
    c.jump = Resume(g_rs7_resume_CLBA);
}

namespace {
// CLGU's join after the ghost prefix read: EDX = the string (the stock `movzx edx,al`), ECX = its ghost string.
void JoinGhost(Context& c, uint32_t s, uint32_t ghost) {
    c.edx = s;
    c.ecx = ghost;
    c.jump = Resume(g_rs7_resume_CLGU, 4);
}
}  // namespace

// CLGU - 0x7E3ED5 (AL = string, DL = the chart bend, ECX = owner, ESI = InstrumentInfo): stock `cmp al,[esi+0Bh]; jae
// reject; cmp dl,0FFh; je reject; movzx edx,al` and the following `mov ecx,[ecx+edx*4+1Ch]` (the ghost prefix entry,
// joined after: resume + 4). Strings 6/7 take the native ghost string. Reject: 0x7E3F14 (resume + 32h).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CLGU(Context& c) {
    const uint32_t s = Low8(c.eax);
    const uint32_t reject = Resume(g_rs7_resume_CLGU, 0x32);

    if (s >= 6) {
        const uint32_t ghost = Low8(c.edx) == 0xFF ? 0 : NativeString(c.ecx, s, 1);

        if (!ghost) {
            c.jump = reject;
            return;
        }

        JoinGhost(c, s, ghost);
        return;
    }

    if (!InStockCount(c, s, c.esi)) {
        c.jump = reject;
        return;
    }

    c.Compare(Low8(c.edx), 0xFF, 8);

    if (Low8(c.edx) == 0xFF) {
        c.jump = reject;
        return;
    }

    JoinGhost(c, s, Dword(c.ecx + s * 4 + 0x1C));
}

// CLGE - 0x7E4347 (CL = string, EDX = owner, ESI = InstrumentInfo; EAX = the tree node, kept): stock `cmp
// cl,[esi+0Bh]; jae reject; movzx ecx,cl; mov ecx,[edx+ecx*4+1Ch]` (the ghost string). Reject: 0x7E4373 (resume + 20h).
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CLGE(Context& c) {
    const uint32_t s = Low8(c.ecx);

    if (s < 6 && !InStockCount(c, s, c.esi)) {
        c.jump = Resume(g_rs7_resume_CLGE, 0x20);
        return;
    }

    const uint32_t ghost = s >= 6 ? NativeString(c.edx, s, 1) : Dword(c.edx + s * 4 + 0x1C);

    if (s >= 6 && !ghost) {
        c.jump = Resume(g_rs7_resume_CLGE, 0x20);
        return;
    }

    c.ecx = ghost;
    c.jump = Resume(g_rs7_resume_CLGE);
}

// CLVB - 0x7E4178, the stop-pluck path (EAX = owner, CL = string, EDX = InstrumentInfo): stock `cmp cl,[edx+0Bh]; jae
// reject; movzx ecx,cl; mov esi,[eax+ecx*4+4]`, then the call of 0x5992E0 with ESI = the main string (strings 6/7: the
// native main string, never prefix + 4*s; ECX keeps its value as the old stub left it). Reject: resume + 5, after the
// call.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CLVB(Context& c) {
    const uint32_t s = Low8(c.ecx);

    if (s >= 6) {
        const uint32_t main = NativeString(c.eax, s, 0);

        if (main) {
            c.esi = main;
        }
        c.jump = Resume(g_rs7_resume_CLVB, main ? 0 : 5);
        return;
    }

    if (!InStockCount(c, s, c.edx)) {
        c.jump = Resume(g_rs7_resume_CLVB, 5);
        return;
    }

    c.ecx = s;
    c.esi = Dword(c.eax + s * 4 + 4);
    c.jump = Resume(g_rs7_resume_CLVB);
}
