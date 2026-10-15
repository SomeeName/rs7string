#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rs7diagram {
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

void Rs7DiagramMovie_Configure(const wchar_t* ini);
