#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Rs7HookTable;
// The chord-diagram data hooks ([chords] diagrams7, group Diagrams): DGB DGM DGS TP13 and the X_D* refs.
extern const Rs7HookTable kRs7DiagramHooks;

// Seven/eight-row chord diagrams ([chords] diagrams7, docs/ARCHITECTURE.md section 9): the stock diagram
// serializers run, then the LowB / LowFs data keys are added; the three owner movies are swapped for recipes from
// chord-diagrams\ that draw the extra rows.
// Logic: diagrams.cpp (the keys of a shape), movie.cpp (the recipe movie swap). Glue: hooks.cpp (the data
// callbacks, song bindings), hooks_movie.cpp (the FileOpener hook), hooks_mid.cpp (the mid-hooks),
// registration.cpp (kRs7DiagramHooks).
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

constexpr size_t kMovieLimit = 16 * 1024 * 1024;  // largest movie or recipe accepted
// False leaves out untouched. Only uncompressed owner GFX/FWS movies are supported.
bool SwapMovie(const uint8_t* movie, size_t n, const uint8_t* recipe, size_t rn, uint16_t sprite,
               std::vector<uint8_t>* out);
int MovieTarget(const char* name);
// Swaps the IFile inside a Scaleform File wrapper returned by FileOpener::OpenFile. On failure the
// wrapper is untouched and *why (optional) names the reason.
bool SwapWrapperFile(void* wrapper, const char* url, const char** why);
// Patches the FileOpener vtable slot once it reads `expect`; false = not yet / failed.
bool InstallOpenHook(uintptr_t* slot, uintptr_t expect);
// Test seams.
void* OpenHookForTest();
void SetOpenForTest(void* fn);
void SetRecipeForTest(int index, const std::vector<uint8_t>& recipe);
}  // namespace rs7diagram

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_diagrams;
__declspec(dllexport) void __cdecl Rs7Diagram_Manifest(void* object, void* frets, void* fingers, void* attributes,
                                                       void* chord, int capo);
__declspec(dllexport) void __cdecl Rs7Diagram_Loaded(void** holder, const uint8_t* tpl);
__declspec(dllexport) void __cdecl Rs7Diagram_Bind(void* song, void* attributes);
}
void Rs7Diagram_Drop(void* song);
int Rs7Diagram_Bindings();  // leak audit: songs with a bound dictionary (reference held until the song is freed)
// [chords] diagrams7 and, with it on, the movie recipes ([debug] diagrams_trace). Read at startup.
void Rs7Diagram_Configure(const wchar_t* ini);
void Rs7DiagramMovie_Configure(const wchar_t* ini);
