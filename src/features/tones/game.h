#pragma once
#include <cstddef>
#include <cstdint>

namespace rsxrtones {
uint32_t Read(uint32_t base, size_t offset = 0);
uint32_t Attributes(uint32_t arrangement);
bool Name(uint32_t attributes, const char* key, char* text, size_t size);
uint32_t Definition(uint32_t collection, const char* key);
// Retained canonical object from CustomTone/<player>/LAS_ToneCustomData_<extension>, or zero.
uint32_t Override(uint32_t player, uint32_t slot);
bool Reference(uint32_t object, bool retain);
bool Apply(uint32_t object, uint32_t player);
}  // namespace rsxrtones
