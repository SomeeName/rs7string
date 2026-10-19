// The three guarded pitch readers (PUBNL PUBAN PUBAC): native copies built from tools/gen_publish_pitch.py, chosen per
// call by Rs7PublishPitch_Target (entry mid-hooks in hooks_entries.cpp), the chord tail their copy calls, the expected
// pitch over the tail the PUBAN copy calls, and the standard-zero expected-pitch sites PUBZ0-PUBZ6.
#include "features/nslot/nslot.h"
#include "features/pitch/pitch.h"
#include "features/publish/hooks.h"
#include "platform/fncopy.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <windows.h>
#include <climits>
#include <cstring>

extern "C" {
#define PAIR(id)                                                                                                       \
    __declspec(dllexport) uintptr_t g_rs7_pitchcopy_##id = 0;                                                          \
    __declspec(dllexport) uintptr_t g_rs7_pitchtramp_##id = 0;
PAIR(PUBNL)
PAIR(PUBAN)
PAIR(PUBAC)
#undef PAIR
}

namespace {
struct PitchRel {
    unsigned off;
    uintptr_t target;
    Rs7Mid::CallIn* callin = nullptr;  // a call-in entry (hooks_callin.cpp) instead of target
};

// A rel target the generated table marks for the expected-pitch call entry built at run time (g_tailPitchCall).
constexpr uintptr_t kTailPitchCall = 1;

struct PitchSpec {
    const char* id;
    uintptr_t va;
    unsigned stockLen, hash, entry;
    const unsigned char* code;
    unsigned len;
    const PitchRel* rels;
    unsigned nrels;
};

#include "features/publish/publish_pitch.generated.h"

template <class T>
T Read(const uint8_t* p, unsigned off) {
    T v;

    std::memcpy(&v, p + off, sizeof v);

    return v;
}

uint8_t* Detector(uint8_t* object, unsigned kind) {
    if (!object) {
        return nullptr;
    }

    if (kind) {
        object = Read<uint8_t*>(object, 0x4c19c);
        if (!object) {
            return nullptr;
        }
    }

    return Read<uint8_t*>(object, 4);
}

bool Eligible(uint8_t* d) {
    if (!g_rs7_publish_ports || !Rs7Pitch_Tailed(d, 1)) {
        return false;
    }

    const unsigned n = Read<unsigned>(d, rs7p::kDetCount);

    return n >= 6 && n <= 8 && Read<int>(d, 0x11ec) != 2;
}

struct Pair {
    uintptr_t* copy;
    uintptr_t* tramp;
};

const Pair pairs[] = {{&g_rs7_pitchcopy_PUBNL, &g_rs7_pitchtramp_PUBNL},
                      {&g_rs7_pitchcopy_PUBAN, &g_rs7_pitchtramp_PUBAN},
                      {&g_rs7_pitchcopy_PUBAC, &g_rs7_pitchtramp_PUBAC}};

void Relative(unsigned char* p, uintptr_t target) {
    int32_t r = int32_t(target - reinterpret_cast<uintptr_t>(p + 4));

    std::memcpy(p, &r, 4);
}

// The expected pitch 0x873F80 computes, over an eight-entry tuning (ECX = the int16 tuning, EDX = instrument, ESI =
// string, [esp+4] = fret; plain ret, the caller pops the fret): EAX = the pitch, EDX = instrument - 2 as the stock
// cases leave it, ECX / ESI kept; a null tuning gives 0 with EDX kept. Answered for the callee (Before placement).
void TailPitch(Rs7Mid::Context& c, const int16_t* tuning8) {
    if (!tuning8) {
        Rs7Mid::Return(c, 0);
        return;
    }

    const uint32_t instrument = c.edx;

    c.edx = instrument - 2;
    Rs7Mid::Return(c, rs7p::ExpectedPitch8(tuning8, instrument, c.esi, Rs7Mid::Dword(c.esp + 4)));
}

// The PUBAN copy's call (light; prepared once, lives with the copies).
Rs7Mid::Hook g_tailPitchCall;
}  // namespace

extern "C" {
// The PUBAN copy's call of the expected pitch (0x8754AA's call of 0x873F80 in the stock function, the copy passes the
// detector's tuning8 tail +13B8h in ECX): every string over the tail. Light (per analysed mistake, integer only). It
// replaced the hand-written Rs7_Pitch_Raw8 (frozen at 72da588, tools/tests/test_midhook_asm_sites2.py).
__declspec(dllexport) void __cdecl Rs7_Mid_PubTailPitch(Rs7Mid::Context& c) {
    TailPitch(c, Rs7Mid::Ptr<const int16_t>(c.ecx));
}

// PUBZ0-PUBZ6: the calls (PUBZ0, PUBZ4-6: call sites, Before placement) and tail jumps (PUBZ1-3: jmp sites at
// `jmp 873F80h`, the replay is the relocated jmp) of 0x873F80 with the standard (zero) tuning 0x1199D2C in ECX; at
// every one [esp] is the caller's return address and [esp+4] the fret. With the publication ports, guitar strings 6/7
// get the expected pitch over the all-zero eight-entry tuning g_rs7_publish_zero8 (stock: base -1 past the six-entry
// table); everything else runs the stock function (with S1). ECX keeps the caller's tuning pointer, as the stock callee
// does (the old stub left g_rs7_publish_zero8 in it). CONFIRMED dead at every caller (disassembly of the 7
// continuations): PUBZ0 `add esp,4; pop esi; pop ebp; ret`; PUBZ1-3 are the three tail jumps of 0x48DC00, whose only
// caller 0x77EA61 continues `cdq; mov [edi],eax; ...; ret` without reading ECX; PUBZ4 `mov ecx,[ebp+8]`; PUBZ5 `pop
// ecx` before its `ret`; PUBZ6 `lea ecx,[ebp-0Ch]` before any read. Light: integer only. Converted from the naked stub
// Rs7_Publish_ZeroRaw (frozen at 72da588, tools/tests/test_midhook_asm_sites2.py); its flags were dead at the return.
__declspec(dllexport) uintptr_t g_rs7_target_PUBZ0 = 0, g_rs7_target_PUBZ4 = 0, g_rs7_target_PUBZ5 = 0,
                                g_rs7_target_PUBZ6 = 0;

__declspec(dllexport) void __cdecl Rs7_Mid_PUBZ(Rs7Mid::Context& c) {
    if (!g_rs7_publish_ports || c.esi < 6 || c.esi > 7 || c.edx == 2) {
        return;
    }

    TailPitch(c, g_rs7_publish_zero8);
}
}

extern "C" {
__declspec(dllexport) const int16_t g_rs7_publish_zero8[8] = {};
}

namespace {
// The light call entry of the PUBAN copy's expected-pitch call at `call` (E8 rel32, its callee 0x873F80 never runs).
uintptr_t TailPitchEntry(const unsigned char* call) {
    if (!g_tailPitchCall.Entry()) {
        constexpr uintptr_t kExpectedPitch = 0x873F80;
        const uintptr_t site = reinterpret_cast<uintptr_t>(call);
        const int32_t rel = int32_t(kExpectedPitch - (site + 5));
        uint8_t bytes[5] = {0xe8};

        std::memcpy(bytes + 1, &rel, 4);
        if (!g_tailPitchCall.PrepareCall(site, bytes, 5, &Rs7_Mid_PubTailPitch, Rs7Mid::Mode::Light)) {
            return 0;
        }
    }

    return g_tailPitchCall.Entry();
}
}  // namespace

bool Rs7PublishPitch_BuildCopies() {
    // Dedicated builder: variable-length stack/branch edits and the 3KB note matcher cannot
    // use fncopy's 800h temporary buffer. Every original byte is verified before installation.
    for (unsigned k = 0; k < 3; ++k) {
        const auto& s = kPitchSpecs[k];

        if (Rs7Fn_Fnv(reinterpret_cast<const uint8_t*>(s.va), s.stockLen) != s.hash) {
            Rs7Log("native_publish: pitch reader %s bytes differ", s.id);
            return false;
        }

        auto p = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, s.len + s.entry + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!p) {
            return false;
        }

        std::memcpy(p, s.code, s.len);

        for (unsigned r = 0; r < s.nrels; ++r) {
            uintptr_t target = s.rels[r].target;

            if (s.rels[r].callin && !(target = s.rels[r].callin->Entry())) {
                Rs7Log("native_publish: pitch reader %s: no call-in entry", s.id);
                return false;
            }

            if (target == kTailPitchCall && !(target = TailPitchEntry(p + s.rels[r].off - 1))) {
                Rs7Log("native_publish: pitch reader %s: no expected-pitch entry", s.id);
                return false;
            }

            Relative(p + s.rels[r].off, target);
        }

        auto t = p + s.len;

        std::memcpy(t, reinterpret_cast<void*>(s.va), s.entry);
        t[s.entry] = 0xe9;
        Relative(t + s.entry + 1, s.va + s.entry);
        FlushInstructionCache(GetCurrentProcess(), p, s.len + s.entry + 5);
        *pairs[k].copy = reinterpret_cast<uintptr_t>(p);
        *pairs[k].tramp = reinterpret_cast<uintptr_t>(t);
    }

    return true;
}

extern "C" uintptr_t __cdecl Rs7PublishPitch_Target(unsigned kind, uint8_t* object) {
    if (kind >= 3) {
        return 0;
    }

    return *pairs[kind].copy && Eligible(Detector(object, kind)) ? *pairs[kind].copy : *pairs[kind].tramp;
}

extern "C" void __cdecl Rs7PublishPitch_ChordTail(int32_t* out, const uint8_t* tpl, const uint8_t* tracker) {
    const uint8_t* t8 = tpl ? Rs7Ns_AsT8(tpl) : nullptr;
    auto det = Detector(const_cast<uint8_t*>(tracker), 1);
    const unsigned n = det ? Read<unsigned>(det, rs7p::kDetCount) : 6;

    for (unsigned s = 6; s < 8; ++s) {
        out[s] = t8 && s < n ? Read<int32_t>(t8, rs7ns::kT8Midi + 4 * s) : -1;
    }
}
