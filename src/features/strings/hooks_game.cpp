// Calls into the game for the native GuitarStrings (hooks.h), and the two answers the copies' calls need from it:
// the attach point and the atlas row of an added string (the call-ins Rsxr_CallIn_Attach / Rsxr_CallIn_Shape,
// hooks_callin.cpp).
#include "features/scene/scene.h"
#include "features/spacing/spacing.h"
#include "features/string7/string7.h"
#include "features/strings/hooks.h"
#include "model/instrument.h"
#include "platform/log.h"
#include <windows.h>
#include <cmath>
#include <cstring>

using namespace rsxrgs;

uint8_t* rsxrgs::GameAlloc(uint32_t size) {
    using Fn = uint8_t*(__cdecl*)(uint32_t, uint32_t);

    return g_rsxr_fn_alloc ? reinterpret_cast<Fn>(g_rsxr_fn_alloc)(size, 0) : nullptr;
}

void rsxrgs::GameStrCtor(uint8_t* obj, uint32_t kind) {
    if (!g_rsxr_fn_strctor) {
        return;
    }

    __asm {
        push ebx
        push esi
        push edi
        mov esi, obj
        mov eax, kind
        call dword ptr [g_rsxr_fn_strctor]
        pop edi
        pop esi
        pop ebx
    }
}

void rsxrgs::GameShow(uint8_t* entity, uint32_t show) {
    rsxrs7d::ShowEntity(entity, show);
}

void rsxrgs::GameDetach(uint8_t* entity, uint32_t detach) {
    using Fn = uint32_t(__cdecl*)(uint8_t*, uint32_t, uint32_t);

    if (g_rsxr_fn_detach) {
        reinterpret_cast<Fn>(g_rsxr_fn_detach)(entity, detach, 1);
    }
}

void rsxrgs::GameFade(uint8_t* entity, bool in, float seconds) {
    rsxrs7d::FadeEntity(in ? g_rsxr_fn_fadein : g_rsxr_fn_fadeout, entity, seconds);
}

namespace {
// Rnd_GetStringAttachPoint 0x594530: ECX = info, cdecl(s, &vec3) -> AL.
bool GameAttach(uint8_t* info, uint32_t s, float* outVec) {
    uint32_t ok = 0;

    if (!g_rsxr_fn_attach) {
        return false;
    }

    __asm {
        push ebx
        push esi
        push edi
        push outVec
        push s
        mov ecx, info
        call dword ptr [g_rsxr_fn_attach]
        add esp, 8
        movzx eax, al
        mov ok, eax
        pop edi
        pop esi
        pop ebx
    }

    return ok != 0;
}
}  // namespace

extern "C" int __cdecl RsxrGs_AttachExt(uint8_t* info, uint32_t s, float* out) {
    s &= 0xFF;
    const bool bass = StockAttachCount(info) == 4;

    if (s >= rsxr::MaxSlots(bass)) {
        return 0;
    }

    uint32_t nearRow = 0, farRow = 1;
    float a0[3], a1[3];

    if (bass) {
        BassAttachRows(s, &nearRow, &farRow);
    }
    std::memcpy(a0, out, sizeof a0);
    std::memcpy(a1, out, sizeof a1);
    if (!GameAttach(info, nearRow, a0) || !GameAttach(info, farRow, a1)) {
        return 0;
    }

    ExtrapolateAttach(a0, a1, bass ? 6u : s, out);  // s 6: 2*a0 - a1, s 7: 3*a0 - 2*a1; bass: one spacing
    if (RsxrLog_Upto(&g_strings.logAttach, 2)) {
        RsxrLog("gsnative: string %u attach (%.3f, %.3f, %.3f) from strings 0/1", s, double(out[0]), double(out[1]),
                double(out[2]));
    }

    return 1;
}

namespace {
// Set while this thread is inside RsxrGs_AttachSpaced: its own attach queries re-enter PUBGA and must get the stock
// answer. Per thread, so two guitars built on parallel loader jobs (co-op) both get the spacing.
thread_local bool t_inSpaced = false;
volatile long g_logSpaced = 0;
}  // namespace

extern "C" int __cdecl RsxrGs_AttachSpaced(uint8_t* info, uint32_t s, float* out) {
    s &= 0xFF;
    if (!info || !out) {
        return -1;
    }

    // The setting first: the stock path (no setting) makes no import call (the emulator tests run it).
    const uint32_t stock = StockAttachCount(info);
    const float want =
        rsxrspacing::Target(g_rsxr_spacing, stock == 4, UncheckedRead<uint8_t>(info, rsxr::kInfoStrings));

    if (want <= 0.0f) {
        RsxrSpacing_Forget(info);  // a reused InstrumentInfo address never keeps an older build's factor
        return -1;
    }

    if (t_inSpaced) {
        return -1;
    }

    float p[3], r0[3], r1[3], rl[3];
    bool ok = false;

    std::memcpy(p, out, sizeof p);
    std::memcpy(r0, out, sizeof r0);
    std::memcpy(r1, out, sizeof r1);
    std::memcpy(rl, out, sizeof rl);

    t_inSpaced = true;

    __try {
        ok = (s < stock ? GameAttach(info, s, p) : RsxrGs_AttachExt(info, s, p) != 0) && GameAttach(info, 0, r0) &&
             GameAttach(info, 1, r1) && GameAttach(info, stock - 1, rl);
    } __finally {
        t_inSpaced = false;
    }

    if (!ok) {
        return s < rsxr::MaxSlots(stock == 4) ? -1 : 0;
    }

    float d2 = 0.0f, c[3];

    for (int i = 0; i < 3; ++i) {
        d2 += (r0[i] - r1[i]) * (r0[i] - r1[i]);
        c[i] = 0.5f * (r0[i] + rl[i]);
    }

    if (d2 <= 1e-8f) {
        std::memcpy(out, p, sizeof p);
        return 1;
    }

    const float k = want / std::sqrt(d2);

    rsxrspacing::Scale(c, k, p);
    std::memcpy(out, p, sizeof p);
    RsxrSpacing_Record(info, k);

    if (RsxrLog_Upto(&g_logSpaced, 16)) {
        RsxrLog("spacing: %s %u-string string %u attach (%.3f, %.3f, %.3f), spacing %.3f -> %.3f (k %.3f)",
                stock == 4 ? "bass" : "guitar", unsigned(UncheckedRead<uint8_t>(info, rsxr::kInfoStrings)), s,
                double(p[0]), double(p[1]), double(p[2]), double(std::sqrt(d2)), double(want), double(k));
    }

    return 1;
}

extern "C" void __cdecl RsxrGs_ShapeDone(uint8_t* obj, uint32_t s) {
    s &= 0xFF;
    if (!obj || !UncheckedRead<uint8_t>(obj, 0x50) || s < 4 || s > 7) {
        return;
    }

    // Strings 4 / 5 only get here on a bass (a guitar's are below its stock attach count, the stock lookup alone):
    // the stock lookup indexed the four-entry bass row table past its end into the guitar rows (0.4 / 0.5), so the
    // low B and the high C drew a guitar row of the bass texture. They take the E's and the G's rows instead.
    const float v = s >= 6 ? g_rsxr_ns_v[s - 6] : rsxrgs::BassStringRow(s);

    if (uint8_t* h = UncheckedRead<uint8_t*>(obj, 0x3C)) {
        UncheckedField<float>(h, 0x14) = v;
    }
    if (RsxrLog_Upto(&g_strings.logShape, 2)) {
        RsxrLog("gsnative: string %u (%s %p) shader params found, atlas row %.2f", s,
                UncheckedRead<uint32_t>(obj, 0) ? "ghost" : "main", obj, double(v));
    }
}
