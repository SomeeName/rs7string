#pragma once
#include <cstdint>

namespace rs7rsmods {
struct Colour {
    bool valid = false;
    uint32_t rgb = 0;
};

Colour Read(const wchar_t* iniPath, bool loaded);
// Inspect an installed proxy's resources without executing it, then read its adjacent INI.
Colour ReadProxy(const wchar_t* proxyPath);
void Resolve(const Colour& colour, bool enabled, uint32_t (&rgb)[2]);
// Startup only. Overlay changes use the cached policy.
void Init();
const Colour& Cached();
}  // namespace rs7rsmods
