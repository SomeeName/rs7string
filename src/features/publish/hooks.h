#pragma once
#include "features/publish/publish.h"
#include "platform/midhook.h"
#include <cstdint>

// The publication glue's entry points inside src/features/publish/: what the stubs call, what startup builds.

// The relocated copies (hooks.cpp: PUBCL PUBTO PUBWU, tools/gen_publish.py; hooks_pitch.cpp: PUBNL PUBAN PUBAC,
// tools/gen_publish_pitch.py). False: that copy cannot be built, the ports go off.
bool Rs7Publish_BuildCopies();
bool Rs7PublishPitch_BuildCopies();

extern "C" {
__declspec(dllexport) void __cdecl Rs7Publish_Configure(int requested);

// ---- preparation copies (hooks.cpp, hooks_entries.cpp, hooks_callin.cpp)
__declspec(dllexport) uintptr_t __cdecl Rs7Publish_Target(unsigned kind, uint8_t* reg);
__declspec(dllexport) int __cdecl Rs7Publish_Classify(uint8_t* tracker, uint8_t* record, int midi, int initial);

// ---- pitch readers (hooks_pitch.cpp, hooks_entries.cpp, hooks_stubs_pitch.cpp); PUBZ0-PUBZ6 (hooks_pitch.cpp)
__declspec(dllexport) uintptr_t __cdecl Rs7PublishPitch_Target(unsigned kind, uint8_t* object);
__declspec(dllexport) void __cdecl Rs7PublishPitch_ChordTail(int32_t* out8, const uint8_t* tpl, const uint8_t* tracker);
__declspec(dllexport) extern const int16_t g_rs7_publish_zero8[8];

// ---- the grid (hooks_grid.cpp, hooks_gridsites.cpp)
// GridBuild's per-grid wait table (8 entries), exported for the tests.
__declspec(dllexport) extern rs7pubgrid::WaitEntry g_rs7_publish_wait[8];
__declspec(dllexport) int __cdecl Rs7Publish_GridWait(uint8_t* grid, unsigned reason);
__declspec(dllexport) int __cdecl Rs7Publish_GridPos(uint8_t* grid, unsigned fret, unsigned k, unsigned string,
                                                     float* out);
__declspec(dllexport) int __cdecl Rs7Publish_GridBuild(uint8_t* grid, uint8_t* guitar);
__declspec(dllexport) int __cdecl Rs7Publish_FingerSpan(const uint8_t* tpl, unsigned finger, int* span);
__declspec(dllexport) unsigned __cdecl Rs7Publish_Edge(uint8_t* info, unsigned first);
}

// The pitch copies' call-in entries (hooks_callin.cpp; hooks_pitch.cpp writes them into the copies).
extern Rs7Mid::CallIn g_rs7_callin_PubChordTail, g_rs7_callin_ReverseTail;
