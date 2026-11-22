#pragma once
#include <cstdint>

// The switches a hook row can depend on (RsxrHook::gates, set with RsxrHook::Gate in a feature's registration.cpp).
// The platform only stores the bits; Rsxr_ShouldPatch (src/hook_rules.cpp) writes a row only when every switch its
// bits name is satisfied (including exclusions).
constexpr uint32_t kRsxrGatePegs = 1u << 0;      // the native N-slot PegSet (headstock/pegset.h)
constexpr uint32_t kRsxrGateStrings = 1u << 1;   // the native N-slot GuitarStrings (strings.h)
constexpr uint32_t kRsxrGateFretgrid = 1u << 2;  // the native capacity-8 FretGrid ([neck] native_fretgrid)
constexpr uint32_t kRsxrGatePitch = 1u << 3;     // the native 8-entry tuning / open-MIDI storage ([neck] native_pitch)
constexpr uint32_t kRsxrGateNextgrid = 1u << 4;  // the native capacity-8 next-note grid
constexpr uint32_t kRsxrGatePublish = 1u << 5;   // verified preparation ports: request + all five native prerequisites
constexpr uint32_t kRsxrGateTuner = 1u << 6;     // the native 8-slot tuner and its prerequisites
constexpr uint32_t kRsxrGateClite = 1u << 7;
constexpr uint32_t kRsxrGateNoClite = 1u << 8;
constexpr uint32_t kRsxrGatePublishTrace = 1u << 9;
constexpr uint32_t kRsxrGateNoPublish = 1u << 10;
constexpr uint32_t kRsxrGateChordloops = 1u << 11;
constexpr uint32_t kRsxrGateColours = 1u << 12;
constexpr uint32_t kRsxrGateTestVariant = 1u << 13;
constexpr uint32_t kRsxrGateGlow = 1u << 14;
constexpr uint32_t kRsxrGateDiagrams = 1u << 15;
constexpr uint32_t kRsxrGateMenuLoad = 1u << 16;
