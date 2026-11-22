#pragma once
#include <cstddef>
#include <cstdint>

struct RsxrHookTable;
// X_WORLD, X_ROOT and the ClipFade references (data references, group Core): registration.cpp.
extern const RsxrHookTable kRsxrSceneHooks;

// The scene-graph reads: SEH-guarded game-memory helpers (read-only, never throw), shared by the
// native strings, frets, pegs, headstock model, tuner and tuning names.

extern "C" {
__declspec(dllexport) extern uintptr_t g_rsxr_fn_world;  // X_WORLD 0x411130 cdecl() -> world (render service)
__declspec(dllexport) extern uintptr_t g_rsxr_fn_root;   // X_ROOT  0x9BD3F0 ECX = entity, (world) ret 4 -> root node
}

namespace rsxrs7d {
// Entity_Show: EAX entity, cdecl(show). Requires a valid entity; does nothing without the resolved callee.
void ShowEntity(uint8_t* entity, uint32_t show);
// ClipFade: EAX entity, (manager, seconds, 0, 1), ret 10h. Does nothing without entity, callee or manager.
void FadeEntity(uintptr_t fn, uint8_t* entity, float seconds);
// Used by features/frets, headstock and strings:
// the entity's root scene node exactly as Rnd_FindStringBaseShape 0x5996C4..0x5996EE finds it
// (entity +0D8h bit 0 -> +0DCh, else World_GetEntityRootNode 0x9BD3F0), nullptr if unavailable;
uint8_t* EntityRoot(uint8_t* entity);
// whether obj's NiRTTI chain names `kind` (e.g. "NiMesh"); its RTTI and scene-graph name.
bool IsKindOf(const void* obj, const char* kind);
void KindOf(const void* obj, char* out, size_t cap);
void NameOfNode(const void* obj, char* out, size_t cap);
}  // namespace rsxrs7d

// The game's ClipFade (refs, never written), used by the native strings and pegs (features/strings,
// features/headstock).
extern "C" {
__declspec(dllexport) extern uintptr_t g_rsxr_fade_mgr;  // X_FADEMGR &ClipFade manager (0x1360480)
__declspec(dllexport) extern uintptr_t
    g_rsxr_fn_fadein;  // X_FADEIN  ClipFade_FadeInEntity 0x824E70 (EAX entity; mgr, s, 0, 1) ret 10h
__declspec(dllexport) extern uintptr_t g_rsxr_fn_fadeout;  // X_FADEOUT ClipFade_FadeOutEntity 0x825000 (same)
}
