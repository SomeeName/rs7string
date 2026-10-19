#pragma once
#include "model/groups.h"

// The hook groups' state, defined by the app layer because it reads every feature's table and switches:
// src/registry.cpp (the table list, the lookup, kRs7Groups) and src/hook_rules.cpp (the failure state, the unit
// cascade and the write conditions). Features get what they need as arguments (Rs7Plain_Start).

const Rs7Hook* Rs7_FindHook(const char* id);
// The group of a hook that Rs7_FindHook returned.
Rs7Feature Rs7_FeatureOf(const Rs7Hook& h);
// Marks the group failed: Rs7_ShouldPatch is false for all its sites from now on and the group's runtime
// switch is cleared (native colours -> stock six-column manager with the legacy C1-C5 colour sites).
// Returns false (and changes nothing) for Core. Idempotent.
bool Rs7_DisableFeature(Rs7Feature f);
bool Rs7_FeatureDisabled(Rs7Feature f);
void Rs7_ResetFeatureFailures();  // tests
// Whether Rs7_Startup writes the hook's site (every site is resolved either way; ref sites never).
bool Rs7_ShouldPatch(const Rs7Hook& h, bool diagnostics, bool vguitar = false, bool nativePegs = false,
                     bool nativeStrings = false);
// The groups as platform/isolate.h's failure policy (src/registry.cpp).
struct Rs7GroupPolicy;
extern const Rs7GroupPolicy kRs7Groups;
// Rs7_FindHook and kRs7Groups for the plain-PSARC unit (features/psarc/psarc.h).
struct Rs7PlainHost;
extern const Rs7PlainHost kRs7PlainHost;
