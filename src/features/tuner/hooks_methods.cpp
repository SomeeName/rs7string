#include "features/pitch/pitch.h"
#include "features/tuner/hooks.h"
#include "features/tuner/state.h"
#include "features/tuner/tuner.h"
#include <windows.h>

// What runs inside a native tuner session's methods: the calls the session copies make (string order steps, the
// selection) and the mid-hooks in the stock methods that stay uncopied (TS8F* TS8RS TS8DT TS8WR TS8SN* TS8PA TS8PZ).
// Game thread only. The copies reach Rs7Tu8_AdvanceC / Rs7Tu8_FirstC through call-in entries (hooks_callin.cpp).

namespace {
using rs7tuner::At;
using rs7tuner::g_state;
using rs7tuner::Read;

using ReleaseFn = void(__thiscall*)(void*);

void ReleaseRef(uint32_t& slot) {
    void* p = reinterpret_cast<void*>(uintptr_t(slot));

    slot = 0;
    if (!p) {
        return;
    }

    __try {
        void** vt = *static_cast<void***>(p);

        reinterpret_cast<ReleaseFn>(vt[2])(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}  // namespace

// ---- called by the copies ----
extern "C" int __stdcall Rs7Tu8_AdvanceC(uint8_t* session, int dir) {
    if (!Rs7Tuner8_IsNative(session)) {
        return 0;
    }

    const int to = rs7tu::Step(session + rs7tu::kOrder, rs7tuner::Count(session), Read<int>(session, rs7tu::kCur), dir);
    if (to < 0) {
        return 0;
    }

    At<int>(session, rs7tu::kCur) = to;

    return 1;
}

extern "C" uint32_t __stdcall Rs7Tu8_FirstC(uint8_t* session) {
    return Rs7Tuner8_IsNative(session) ? Read<uint8_t>(session, rs7tu::kOrder) : 0;
}

extern "C" void __cdecl Rs7Tu8_OnSelect(uint8_t* session, uint32_t s) {
    __try {
        if (!Rs7Tuner8_IsNative(session)) {
            return;
        }

        rs7tuner::SetLabels(session);
        if (g_rs7_lowrange) {
            Rs7Tuner_LowerDetectorTo(session, rs7tuner::LowestOpen(session));
        }
        if (s < 8) {
            rs7tuner::Log("tuner8: session %p selects string %d, target MIDI %d", session, int(s),
                          Read<int>(session, rs7tu::kOpen + 4 * s));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---- mid-hooks ----
// 5AFB19 (mode 0 entry, EBX session): `mov [ebx+74h], esi(0); mov esi, ebx` replayed, then the first
// string of the order.
extern "C" void __cdecl Rs7_Mid_TS8F1(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ebx));

    if (Rs7Tuner8_IsNative(s)) {
        At<int>(s, rs7tu::kCur) = Read<uint8_t>(s, rs7tu::kOrder);
        c.esi = c.ebx;
        c.jump = 0x5AFB1E;
    }
}

// 5AFD15 (capo step 0x5AFCF0, EBX session, curString just set to 0): `mov edi, [ebx+74h]; call
// UI_PlayerTuner_SelectString` replayed after curString = the first string of the order.
extern "C" void __cdecl Rs7_Mid_TS8F2(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ebx));

    if (Rs7Tuner8_IsNative(s)) {
        At<int>(s, rs7tu::kCur) = Read<uint8_t>(s, rs7tu::kOrder);
    }
}

// 5B02A7 (mode 3 entry 0x5B0270, ESI session, EDI = 0, or 5 for the reverse pass): `call
// UI_PlayerTuner_SelectString` replayed with EDI = the first string of the order (the reverse pass keeps
// the stock 5: the last string of every order).
extern "C" void __cdecl Rs7_Mid_TS8F3(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));

    if (Rs7Tuner8_IsNative(s) && !Read<uint8_t>(s, 0x22D)) {
        c.edi = Read<uint8_t>(s, rs7tu::kOrder);
    }
}

// 5AE76D (session reset 0x5AE720 after ResetStandard, ESI session): the tail takes the standard tuning
// and zero peaks too. The replay (`mov byte [esi+196h], bl`) runs.
extern "C" void __cdecl Rs7_Mid_TS8RS(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    for (int k = 0; k < 8; ++k) {
        At<int>(s, rs7tu::kOpen + 4 * k) = k < 6 ? Read<int>(s, rs7tu::kStockOpen + 4 * k) : rs7tu::kGuitarBase[k];
        At<int>(s, rs7tu::kOffsets + 4 * k) = 0;
        At<float>(s, rs7tu::kPeaks + 4 * k) = 0.0f;
    }
}

// 5AE360 (UI_PlayerTuner dtor entry, ECX session): a native session hands its six entities back to the
// prefix (the stock dtor releases them) and releases strings 6 / 7 itself.
extern "C" void __cdecl Rs7_Mid_TS8DT(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.ecx));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    __try {
        for (int k = 0; k < 8; ++k) {
            uint32_t& pick = At<uint32_t>(s, rs7tu::kPicks + 4 * k);
            uint32_t& l0 = At<uint32_t>(s, rs7tu::kLetters + 8 * k);
            uint32_t& l1 = At<uint32_t>(s, rs7tu::kLetters + 8 * k + 4);

            if (k < 6) {
                At<uint32_t>(s, rs7tu::kStockPicks + 4 * k) = pick;
                At<uint32_t>(s, rs7tu::kStockLetters + 8 * k) = l0;
                At<uint32_t>(s, rs7tu::kStockLetters + 8 * k + 4) = l1;
                pick = l0 = l1 = 0;
            } else {
                ReleaseRef(pick);
                ReleaseRef(l1);
                ReleaseRef(l0);
            }
        }

        At<uint8_t>(s, rs7tu::kNative) = 0;
        rs7tuner::Log("tuner8: session %p destroyed (native tail released)", s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    Rs7Tuner_Refresh();
}

// 5B2D88 (UI_PlayerTuner_CheckWrongInstrument, ESI session): `mov ecx, [ebp+8]; mov edx, [esi+ecx*4+38h]`.
extern "C" void __cdecl Rs7_Mid_TS8WR(Rs7Mid::Context& c) {
    uint8_t* s = reinterpret_cast<uint8_t*>(uintptr_t(c.esi));
    if (!Rs7Tuner8_IsNative(s)) {
        return;
    }

    const uint32_t str = *reinterpret_cast<const uint32_t*>(uintptr_t(c.ebp) + 8);

    c.ecx = str;
    c.edx = str < 8 ? Read<uint32_t>(s, rs7tu::kOpen + 4 * str) : 0;
    c.jump = 0x5B2D8F;
}

// 5B2E10 (UI_PlayerTuner_PlayTunedSound entry, ECX string, [esp+4] session): the string before the
// stock clamp to the six Wwise colours.
extern "C" void __cdecl Rs7_Mid_TS8SN1(Rs7Mid::Context& c) {
    g_state.soundString = c.ecx;
    g_state.soundSession = *reinterpret_cast<const uint8_t* const*>(uintptr_t(c.esp) + 4);
}

// 5B2EA7: `mov eax, [ebp-24h]; mov eax, [eax+edi*4+38h]` - the note name of the string itself (EDI is
// clamped to 5 for the colour name).
extern "C" void __cdecl Rs7_Mid_TS8SN2(Rs7Mid::Context& c) {
    uint8_t* s = *reinterpret_cast<uint8_t* const*>(uintptr_t(c.ebp) - 0x24);
    const uint32_t str = g_state.soundString;

    g_state.soundString = 0xFFFFFFFF;
    if (!Rs7Tuner8_IsNative(s) || s != g_state.soundSession || str >= 8) {
        return;
    }

    c.eax = Read<uint32_t>(s, rs7tu::kOpen + 4 * str);
    c.jump = 0x5B2EAE;
}

// 5B63E0 / 5B66AD (Play_Tuning_ApplyToDetection entry / its one exit): the profile's six strings go to
// the detector and matcher in between (features/pitch/pitch.h g_rs7_profile_apply).
extern "C" void __cdecl Rs7_Mid_TS8PA(Rs7Mid::Context&) {
    g_rs7_profile_apply = 1;
}

extern "C" void __cdecl Rs7_Mid_TS8PZ(Rs7Mid::Context&) {
    g_rs7_profile_apply = 0;
}
