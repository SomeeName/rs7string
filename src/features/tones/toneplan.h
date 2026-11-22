#pragma once
#include <cstdint>

namespace rsxrtones {
// Logical slots: Base=0, A=1, E=5, Z=26. Zero mask leaves the stock builder untouched.
uint32_t PlanSlots(uint32_t attributes);
uint32_t PlanName(uint32_t slot, bool manifestField);
const char* SlotExtension(uint32_t slot);
uint32_t BeginPlan(uint32_t attributes);
void EndPlan(uint32_t previous);
bool PlanActive();
}  // namespace rsxrtones
