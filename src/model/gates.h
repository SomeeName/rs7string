#pragma once
#include <cstdint>

// The switches a hook row can depend on (Rs7Hook::gates, set with Rs7Hook::Gate in a feature's registration.cpp).
// The platform only stores the bits; Rs7_ShouldPatch (src/hook_rules.cpp) writes a row only when every switch its
// bits name is satisfied (including exclusions).
constexpr uint32_t kRs7GatePegs = 1u << 0;      // the native N-slot PegSet (headstock/pegset.h)
constexpr uint32_t kRs7GateStrings = 1u << 1;   // the native N-slot GuitarStrings (strings.h)
constexpr uint32_t kRs7GateFretgrid = 1u << 2;  // the native capacity-8 FretGrid ([neck] native_fretgrid)
constexpr uint32_t kRs7GatePitch = 1u << 3;     // the native 8-entry tuning / open-MIDI storage ([neck] native_pitch)
constexpr uint32_t kRs7GateNextgrid = 1u << 4;  // the native capacity-8 next-note grid
constexpr uint32_t kRs7GatePublish = 1u << 5;   // verified preparation ports: request + all five native prerequisites
constexpr uint32_t kRs7GateTuner = 1u << 6;     // the native 8-slot tuner and its prerequisites
constexpr uint32_t kRs7GateClite = 1u << 7;
constexpr uint32_t kRs7GateNoClite = 1u << 8;
constexpr uint32_t kRs7GatePublishTrace = 1u << 9;
constexpr uint32_t kRs7GateNoPublish = 1u << 10;
constexpr uint32_t kRs7GateChordloops = 1u << 11;
constexpr uint32_t kRs7GateColours = 1u << 12;
constexpr uint32_t kRs7GateTestVariant = 1u << 13;
constexpr uint32_t kRs7GateGlow = 1u << 14;
constexpr uint32_t kRs7GateDiagrams = 1u << 15;
constexpr uint32_t kRs7GateMenuLoad = 1u << 16;
