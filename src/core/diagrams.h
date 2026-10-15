#pragma once
#include <cstdint>

namespace rs7diagram {
struct Shape {
    int count = 6;
    int frets[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int fingers[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int capo = 0;
    bool manifest = false;
};

using Put = void (*)(void*, const char*, int);
// Emits only changes/additions to the already completed stock object. 4/6 are no-ops.
void Emit(const Shape& s, Put put, void* ctx);
bool FromT8(const uint8_t* t8, Shape* out);
bool MergeManifestT8(const uint8_t* t8, Shape* shape);
}  // namespace rs7diagram

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_diagrams;
void __cdecl Rs7Diagram_Manifest(void* object, void* frets, void* fingers, void* attributes, void* chord, int capo);
void __cdecl Rs7Diagram_Loaded(void** holder, const uint8_t* tpl);
void __cdecl Rs7Diagram_Bind(void* song, void* attributes);
}
void Rs7Diagram_Drop(void* song);
int Rs7Diagram_Bindings();  // leak audit: songs with a bound dictionary (reference held until the song is freed)
void Rs7Diagram_Configure(const wchar_t* ini);
