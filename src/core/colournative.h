#pragma once
#include "hooks/midhook.h"
#include <cstdint>

// CONFIRMED stock layout: section 4 fretgrid-tables.md. Native compact layout:
// prefix/source lists/modes unchanged; stock six-column ABI retained through DD0h.
// Appended [2][28][8] RGB pointers at DD0h and UV floats at 14D0h; size 1BD0h.
// Stock pointer entries alias the native RGB values for RSMods. Owned RGB has process lifetime.
extern "C" __declspec(dllexport) uintptr_t g_rs7_native_colours;

namespace rs7colour {
constexpr uint32_t kPtr = 0xDD0, kUV = 0x14D0, kSize = 0x1BD0;
constexpr int kStates = 28, kModes = 2, kWidth = 8;

inline uint32_t Width() {
    return g_rs7_native_colours ? 8 : 6;
}

inline uint32_t PtrOffset(uint32_t mode, uint32_t state, uint32_t slot = 0) {
    return (g_rs7_native_colours ? kPtr : 0x350) + 4 * ((mode * 28 + state) * Width() + slot);
}

inline uint32_t UVOffset(uint32_t mode, uint32_t state, uint32_t slot = 0) {
    return (g_rs7_native_colours ? kUV : 0x890) + 4 * ((mode * 28 + state) * Width() + slot);
}

const float* GetRGB(const uint8_t* mgr, uint32_t mode, uint32_t state, uint32_t slot);
void Publish(uint8_t* mgr);  // manager build / configured palette publication, never per query
// Settings overlay (Present thread): publishes now when called on the manager's (CMBUILD) thread, else defers.
void PublishCurrent();
// Any thread (the ramp worker, StoreV6): only marks the configured palette as pending; never touches the manager.
void RequestPublish();
bool PublishPending();
// Game thread (CMBUILD, FGHK, the SetTexture hook): publishes a pending request into the finished manager.
// A no-op on any thread other than the one that ran CMBUILD.
void DrainPending();
void ResetThreadForTests();
// Patch time (fix/colours2): the native layout is only valid for a manager the patched ctor builds.
// ref = &g_pRnd_ColorManager (X_CM). Null when the game has not built its manager yet (native OK),
// else the stock 6-column manager that already exists (native_colours must stay off).
void* ExistingManager(uintptr_t ref);
}  // namespace rs7colour

void __cdecl Rs7Colour_CMINIT(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMBUILD(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMUV(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMIX(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMFALL(Rs7Mid::Context& c);
