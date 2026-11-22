#include "features/tones/native_registration.h"
#include "features/tones/game.h"
#include "platform/midhook.h"
#include "platform/seh.h"
#include <windows.h>

extern "C" uintptr_t g_rsxr_tone_register, g_rsxr_tone_destroy;

namespace rsxrtones {
namespace {
thread_local uint32_t g_registerPlayer = 0;

void RegisterEntry(RsxrMid::Context& c) {
    // CONFIRMED: 0x7D6CE0 takes player in EAX; all remaining arguments and cleanup are stdcall.
    c.eax = g_registerPlayer;
    c.jump = static_cast<uint32_t>(g_rsxr_tone_register);
}

RsxrMid::CallIn g_register{&RegisterEntry, RsxrMid::Mode::Full, 12};

uint32_t Address(const void* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}
}  // namespace

NativeRegistration::~NativeRegistration() {
    if (!destroy_) {
        return;
    }

    __try {
        // CONFIRMED: 0x5A8EF0 destroys nested vectors, cdecl(record end, record begin).
        reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(destroy_)(Address(records_.data() + records_.size()),
                                                                       Address(records_.data()));
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        // Do not retry a partially completed resource release.
    }
}

bool NativeRegistration::Register(uint32_t definition, uint32_t player, uint32_t slot) {
    const uintptr_t entry = g_register.Entry();
    if (!definition || player >= 2 || slot < 5 || slot > 26 || !entry || !g_rsxr_tone_register ||
        !g_rsxr_tone_destroy) {
        return false;
    }

    // The stock routine reads only manager+1A4, store+8 and its selected record. Shift a private view,
    // not the live manager pointer: stock slot zero reaches 8 + player*27 + logical slot in our buffer.
    uint32_t view[5]{0, 0, Address(records_.data() + player * 22 + slot), 0, 0};
    uint32_t manager[0x1A8 / 4]{};

    manager[0x1A4 / 4] = Address(view);

    if (!Reference(definition, true)) {
        return false;
    }

    const uint32_t previous = g_registerPlayer;
    bool completed = false;

    destroy_ = g_rsxr_tone_destroy;
    g_registerPlayer = player;

    __try {
        // Stock consumes this call's retained definition reference. Its return register is unspecified.
        reinterpret_cast<void(__stdcall*)(uint32_t, uint32_t, uint32_t)>(entry)(Address(manager), definition, 0);
        completed = true;
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        completed = false;
    }

    g_registerPlayer = previous;
    const Record& record = records_[8 + player * 27 + slot];

    return completed && record.end >= record.begin && record.capacity >= record.end &&
           (record.end - record.begin) % 4 == 0;
}
}  // namespace rsxrtones
