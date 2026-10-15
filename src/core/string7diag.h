#pragma once
#include <cstdint>

// SEH-guarded game-memory helpers (read-only unless named Store, never throw), shared by the native strings, frets,
// pegs, headstock model, tuner and tuning names. The file once held the render diagnostics and the string-0 mirror of
// the stage-1 7th string object S7 (N10 draw-list fix, ClipFade / morph mirrors, string7_gpu_diag); those went in 1.0
// with S7.

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_fn_world;  // X_WORLD 0x411130 cdecl() -> world (render service)
__declspec(dllexport) extern uintptr_t g_rs7_fn_root;   // X_ROOT  0x9BD3F0 ECX = entity, (world) ret 4 -> root node
}

namespace rs7s7d {
// SEH-guarded helpers shared with frets.cpp (read-only, never throw):
// the entity's root scene node exactly as Rnd_FindStringBaseShape 0x5996C4..0x5996EE finds it
// (entity +0D8h bit 0 -> +0DCh, else World_GetEntityRootNode 0x9BD3F0), nullptr if unavailable;
uint8_t* EntityRoot(uint8_t* entity);
// memcpy that zero-fills and returns false on a fault;
bool SafeCopy(void* dst, const void* src, size_t n);
// whether obj's NiRTTI chain names `kind` (e.g. "NiMesh"); its RTTI and scene-graph name.
bool IsKindOf(const void* obj, const char* kind);
void KindOf(const void* obj, char* out, size_t cap);
void NameOfNode(const void* obj, char* out, size_t cap);
// memcpy into game memory, false on a fault.
bool SafeStore(void* dst, const void* src, size_t n);
}  // namespace rs7s7d
