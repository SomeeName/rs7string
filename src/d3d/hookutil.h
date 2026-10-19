#pragma once
#include <cstddef>

// Helpers shared by the device-hook modules (swap_*.cpp, present.cpp): logging that cannot throw into a
// hook, and the module that owns an address (for the install log lines).
namespace rs7d3d {
// Rs7Log that never throws into the hook (std::string allocation inside Rs7Log).
void LogQuiet(const char* fmt, ...);
// File name of the module that contains `addr`, or "no module".
void ModuleName(const void* addr, char* out, size_t n);
}  // namespace rs7d3d
