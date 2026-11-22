#pragma once
#include <cstdint>

// Optional private-only extras of the checkout: maintainer test tools kept out of the public tree (.publicignore) and
// out of a public build (private.props, build.ps1 -Public). With them, private.props sets RSXR_PRIVATE=1 and
// src/features/private/private.h supplies the hooks below; without them every hook is a no-op and they add no sites.
#ifndef RSXR_PRIVATE
#define RSXR_PRIVATE 0
#endif

#if RSXR_PRIVATE
#include "features/private/private.h"
#else
inline void RsxrPrivate_Start(const wchar_t*) {}  // startup, before patching: read their settings

inline bool RsxrPrivate_SitesOn() {
    return false;  // whether their privateOnly sites are written
}

inline void RsxrPrivate_OnSongTuning(void*, const int16_t*, int, bool) {}  // a song's tuning (pitch S4)

inline void RsxrPrivate_FuzzText(int, const wchar_t*) {}  // their text parsers (fuzz/)

constexpr int kRsxrPrivateRefSites = 0;  // their ref sites (tests)
#endif
