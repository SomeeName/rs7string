// Template / ChordNotes pointer redirects (nslot.h): every stride site that turns a template id into
// `begin + id * 48h` (a ChordNotes id into `begin + id * 948h`) gets the song's superset record instead (T8 / C8,
// Rs7Ns_TplRecord / Rs7Ns_CnRecord: the stock record when the vector is not registered). Each callback skips the
// replay and does the replaced instructions' other work itself (the scratch register = id * 9 / id * 948h).
//
// Mid-hooks converted from naked stubs (refactor step 7, frozen at b8c0ed7, tools/tests/test_midhook_nslot.py).
// Light mode (integer only, LIGHT_CALLBACKS in tools/tests/emu_harness.py) for the play-time sites; TP11 and DS1 run
// on the loader thread (SNG_Song_PostProcessNotes) and use full mode. The old stubs left the lookup's flags; no
// continuation reads them (contract "free").
#include "features/nslot/nslot.h"
#include "platform/midhook.h"
#include <cstdint>

using Rs7Mid::Context;

extern "C" {
__declspec(dllexport) uintptr_t g_rs7_resume_TP1 = 0, g_rs7_resume_TP2 = 0, g_rs7_resume_TP3 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_TP4 = 0, g_rs7_resume_TP5 = 0, g_rs7_resume_TP6 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_TP7 = 0, g_rs7_resume_TP8 = 0, g_rs7_resume_TP11 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_TP12 = 0, g_rs7_resume_DS1 = 0;
__declspec(dllexport) uintptr_t g_rs7_resume_CP1 = 0, g_rs7_resume_CP2 = 0;
}

namespace {
using Reg = uint32_t Context::*;

// A `lea scratch,[id+id*8]; lea out,[begin+scratch*8+add]` site: the registers it reads and writes.
struct TplSite {
    Reg begin, id, out, scratch;
    uint32_t add;  // 4: the record's frets
};

void Redirect(Context& c, const TplSite& s, uintptr_t resume) {
    const uint32_t id = c.*s.id;
    const uint32_t record = Rs7Ns_TplRecord(c.*s.begin, id);

    c.*s.scratch = id * 9;
    c.*s.out = record + s.add;
    c.jump = static_cast<uint32_t>(resume);
}

constexpr TplSite kTP1 = {&Context::esi, &Context::edi, &Context::eax, &Context::ecx, 0};
constexpr TplSite kTP2 = {&Context::ebx, &Context::edi, &Context::edi, &Context::edx, 0};
constexpr TplSite kTP3 = {&Context::edi, &Context::ebx, &Context::ebx, &Context::ecx, 0};
constexpr TplSite kTP468 = {&Context::ebx, &Context::edi, &Context::edx, &Context::ecx, 0};
constexpr TplSite kTP5 = {&Context::ecx, &Context::edi, &Context::edi, &Context::edx, 0};
constexpr TplSite kTP7 = {&Context::ebx, &Context::esi, &Context::ebx, &Context::ecx, 0};
constexpr TplSite kTP11 = {&Context::esi, &Context::ecx, &Context::edx, &Context::ecx, 0};
constexpr TplSite kTP12 = {&Context::edi, &Context::ebx, &Context::edi, &Context::edx, 4};
}  // namespace

// TP1 - 0x558A2E SNG_Song_GetChord 0x558A00 (EDI id, ESI begin): `lea ecx,[edi+edi*8]; lea eax,[esi+ecx*8]`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP1(Context& c) {
    Redirect(c, kTP1, g_rs7_resume_TP1);
}

// TP2 - 0x4608AC Rnd_GuitarStrings_UpdateSectionDimming 0x460550: `lea edx,[edi+edi*8]; lea edi,[ebx+edx*8]`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP2(Context& c) {
    Redirect(c, kTP2, g_rs7_resume_TP2);
}

// The same redirect as the GSDM copy's call at TP2 (a call-in entry, features/strings/hooks_callin.cpp): back to the
// copy (the entry's `ret`, the continuation the call-in left in jump). Light.
extern "C" __declspec(dllexport) void __cdecl Rs7_CallIn_TP2(Context& c) {
    Redirect(c, kTP2, c.jump);
}

// TP3 - 0x58C38D finger keep table 0x58C300: `lea ecx,[ebx+ebx*8]; lea ebx,[edi+ecx*8]`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP3(Context& c) {
    Redirect(c, kTP3, g_rs7_resume_TP3);
}

// TP4 / TP6 / TP8 - 0x58CCD7 finger display 0x58CC80, 0x7A003F chord box in 0x79FF30, 0x7E64E1 chord spawn
// 0x7E6400: `lea ecx,[edi+edi*8]; lea edx,[ebx+ecx*8]`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP4(Context& c) {
    Redirect(c, kTP468, g_rs7_resume_TP4);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP6(Context& c) {
    Redirect(c, kTP468, g_rs7_resume_TP6);
}

extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP8(Context& c) {
    Redirect(c, kTP468, g_rs7_resume_TP8);
}

// TP5 - 0x79CFAE chord render in 0x79CDF0: `lea edx,[edi+edi*8]; lea edi,[ecx+edx*8]` (the x87 stack is not empty
// here; the light thunk leaves it alone)
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP5(Context& c) {
    Redirect(c, kTP5, g_rs7_resume_TP5);
}

// TP7 - 0x7A150B chord name 0x7A14A0: `lea ecx,[esi+esi*8]; xor esi,esi; lea ebx,[ebx+ecx*8]`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP7(Context& c) {
    Redirect(c, kTP7, g_rs7_resume_TP7);
    c.esi = 0;
}

// TP11 - 0x55CC1C SNG_Song_PostProcessNotes (section string mask): `lea ecx,[ecx+ecx*8]; lea edx,[esi+ecx*8]`.
// (TP9/TP10 were dropped: those loops read frets the capo rule 0x55B4A2 had just rewritten on the stock vector, and
// stay six-wide.)
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP11(Context& c) {
    Redirect(c, kTP11, g_rs7_resume_TP11);
}

// TP12 - 0x7E2E80 in the chord pluck 0x7E2DE0 (EBX = chord id, EDI = template vector begin):
// `lea edx,[ebx+ebx*8]; lea edi,[edi+edx*8+4]` -> EDI = the T8's frets (+4), EDX = id * 9.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_TP12(Context& c) {
    Redirect(c, kTP12, g_rs7_resume_TP12);
}

// DS1 - double-stop count over the T8 frets. 0x55B9ED in SNG_Song_PostProcessNotes (ECX = chord id, ESI = template
// vector begin): `lea ecx,[ecx+ecx*8]; xor edx,edx; lea eax,[esi+ecx*8+4]; lea ecx,[edx+6]` sets up the count of
// frets != -1 over the template's six frets (the note gets 0x02000000 when it is 2). Here: EAX / ECX = the frets and
// count of rs7ns::DoubleStopSpan (a T8: frets[8] over 7 strings; an unregistered vector: the stock +4 over 6), EDX = 0.
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_DS1(Context& c) {
    const rs7ns::FretSpan span = rs7ns::DoubleStopSpan(c.esi, c.ecx, Rs7Ns_TplRecord(c.esi, c.ecx));

    c.eax = span.frets;
    c.edx = 0;
    c.ecx = span.count;
    c.jump = static_cast<uint32_t>(g_rs7_resume_DS1);
}

// CP1 - 0x558A70 SNG_Song_GetChordNotes 0x558A40 (EAX id, ESI begin): `imul eax,eax,948h; add eax,esi`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CP1(Context& c) {
    c.eax = Rs7Ns_CnRecord(c.esi, c.eax);
    c.jump = static_cast<uint32_t>(g_rs7_resume_CP1);
}

// CP2 - 0x7E6553 chord spawn 0x7E6400 (ESI id, EBX begin): `imul esi,esi,948h; add ebx,esi`
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_CP2(Context& c) {
    c.ebx = Rs7Ns_CnRecord(c.ebx, c.esi);
    c.esi *= rs7ns::kCnSize;
    c.jump = static_cast<uint32_t>(g_rs7_resume_CP2);
}
