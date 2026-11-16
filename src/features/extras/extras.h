#pragma once
#include <cstdint>

// Optional private-only extras of the checkout: maintainer test tools kept out of the public tree (.publicignore) and
// out of a public build (private.props, build.ps1 -Public). With them, private.props sets RS7_PRIVATE=1 and
// src/features/private/private.h supplies the hooks below; without them every hook is a no-op and they add no sites.
#ifndef RS7_PRIVATE
#define RS7_PRIVATE 0
#endif

#if RS7_PRIVATE
#include "features/private/private.h"
#else
inline void Rs7Private_Start(const wchar_t*) {}  // startup, before patching: read their settings

inline bool Rs7Private_SitesOn() {
    return false;  // whether their privateOnly sites are written
}

inline void Rs7Private_OnSongTuning(void*, const int16_t*, int, bool) {}  // a song's tuning (pitch S4)

inline void Rs7Private_FuzzText(int, const wchar_t*) {}  // their text parsers (fuzz/)

constexpr int kRs7PrivateRefSites = 0;  // their ref sites (tests)
#endif
