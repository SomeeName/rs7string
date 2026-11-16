#pragma once
// Helpers shared by the pitch feature's sources (not part of pitch.h's interface).
#include "platform/field.h"
#include <cstdint>

namespace rs7p {
// The field of type T at byte offset `off` of a game object.
using rs7mem::UncheckedField;
}  // namespace rs7p
