#include "core/publish_pitch.h"
#include "core/fncopy.h"
#include "core/nslot.h"
#include "core/pitch8.h"
#include "core/publish.h"
#include "log.h"
#include <windows.h>
#include <climits>
#include <cstring>

extern "C" void Rs7_Call_PubChordTail();
extern "C" void Rs7_Publish_ReverseTail();
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
};

struct PitchSpec {
    const char* id;
    uintptr_t va;
    unsigned stockLen, hash, entry;
    const unsigned char* code;
    unsigned len;
    const PitchRel* rels;
    unsigned nrels;
};

#include "core/publish_pitch.generated.h"

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
}  // namespace

extern "C" {
__declspec(dllexport) const int16_t g_rs7_publish_zero8[8] = {};
}

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
            Relative(p + s.rels[r].off, s.rels[r].target);
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
