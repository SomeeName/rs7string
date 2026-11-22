#pragma once
// Helpers shared by the pitch feature's sources (not part of pitch.h's interface).
#include "platform/field.h"
#include <cstdint>

namespace rsxrp {
// The field of type T at byte offset `off` of a game object.
using rsxrmem::UncheckedField;
}  // namespace rsxrp
