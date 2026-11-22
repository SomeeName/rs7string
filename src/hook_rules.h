#pragma once
#include "model/groups.h"

// The hook groups' state, defined by the app layer because it reads every feature's table and switches:
// src/registry.cpp (the table list, the lookup, kRsxrGroups) and src/hook_rules.cpp (the failure state, the unit
// cascade and the write conditions). Features get what they need as arguments (RsxrPlain_Start).

const RsxrHook* Rsxr_FindHook(const char* id);
// The group of a hook that Rsxr_FindHook returned.
RsxrFeature Rsxr_FeatureOf(const RsxrHook& h);
// Marks the group failed: Rsxr_ShouldPatch is false for all its sites from now on and the group's runtime
// switches are cleared, with the groups that depend on it. Returns false (and changes nothing) for Core. Idempotent.
bool Rsxr_DisableFeature(RsxrFeature f);
bool Rsxr_FeatureDisabled(RsxrFeature f);
void Rsxr_ResetFeatureFailures();  // tests
// Whether Rsxr_Startup writes the hook's site (every site is resolved either way; ref sites never).
bool Rsxr_ShouldPatch(const RsxrHook& h, bool diagnostics, bool privateOn = false, bool nativePegs = false,
                      bool nativeStrings = false);
// The groups as platform/isolate.h's failure policy (src/registry.cpp).
struct RsxrGroupPolicy;
extern const RsxrGroupPolicy kRsxrGroups;
// Rsxr_FindHook and kRsxrGroups for the plain-PSARC unit (features/psarc/psarc.h).
struct RsxrPlainHost;
extern const RsxrPlainHost kRsxrPlainHost;
