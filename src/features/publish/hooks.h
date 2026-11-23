#pragma once
#include "features/publish/publish.h"
#include "platform/field.h"
#include "platform/midhook.h"
#include <cstdint>
#include <cstring>

// The publication glue's entry points inside src/features/publish/: what the stubs call, what startup builds.

// The relocated copies (hooks.cpp: PUBCL PUBTO PUBWU, tools/gen_publish.py; hooks_pitch.cpp: PUBNL PUBAN PUBAC,
// tools/gen_publish_pitch.py). False: that copy cannot be built, the ports go off.
bool RsxrPublish_BuildCopies();
bool RsxrPublishPitch_BuildCopies();

namespace rsxrpubhooks {
constexpr uint32_t kTrackerInput =
    0x4C19C;  // note tracker: the detect-input object (Publish_Classify, the pitch readers)

// An unaligned read of game memory at p + off (callers are on the game thread and have checked p).
using rsxrmem::UncheckedRead;

// The exported addresses of one relocated copy and of its trampoline to the stock function.
struct Pair {
    uintptr_t* copy;
    uintptr_t* tramp;
};
}  // namespace rsxrpubhooks

extern "C" {
__declspec(dllexport) void __cdecl RsxrPublish_Configure(int requested);

// ---- preparation copies (hooks.cpp, hooks_entries.cpp, hooks_callin.cpp)
__declspec(dllexport) uintptr_t __cdecl RsxrPublish_Target(unsigned kind, uint8_t* reg);
__declspec(dllexport) int __cdecl RsxrPublish_Classify(uint8_t* tracker, uint8_t* record, int midi, int initial);

// ---- pitch readers (hooks_pitch.cpp, hooks_entries.cpp, hooks_stubs_pitch.cpp); PUBZ0-PUBZ6 (hooks_pitch.cpp)
__declspec(dllexport) uintptr_t __cdecl RsxrPublishPitch_Target(unsigned kind, uint8_t* object);
__declspec(dllexport) void __cdecl RsxrPublishPitch_ChordTail(int32_t* out8, const uint8_t* tpl,
                                                              const uint8_t* tracker);
__declspec(dllexport) extern const int16_t g_rsxr_publish_zero8[8];

// ---- the grid (hooks_grid.cpp, hooks_gridsites.cpp)
// GridBuild's per-grid wait table (8 entries), exported for the tests.
__declspec(dllexport) extern rsxrpubgrid::WaitEntry g_rsxr_publish_wait[8];
__declspec(dllexport) int __cdecl RsxrPublish_GridWait(uint8_t* grid, unsigned reason);
__declspec(dllexport) int __cdecl RsxrPublish_GridPos(uint8_t* grid, unsigned fret, unsigned k, unsigned string,
                                                      float* out);
__declspec(dllexport) int __cdecl RsxrPublish_GridBuild(uint8_t* grid, uint8_t* guitar);
__declspec(dllexport) int __cdecl RsxrPublish_FingerSpan(const uint8_t* tpl, unsigned finger, int* span);
__declspec(dllexport) unsigned __cdecl RsxrPublish_Edge(uint8_t* info, unsigned first);
__declspec(dllexport) unsigned __cdecl RsxrPublish_FingerAnchor(const uint8_t* tpl, unsigned string,
                                                                const uint8_t* owner);
__declspec(dllexport) extern volatile uint32_t g_rsxr_finger_bass;
__declspec(dllexport) void __cdecl RsxrPublish_BarreFit(const uint8_t* owner, uint8_t* marker, float* duration);
}

// The pitch copies' call-in entries (hooks_callin.cpp; hooks_pitch.cpp writes them into the copies).
extern RsxrMid::CallIn g_rsxr_callin_PubChordTail, g_rsxr_callin_ReverseTail;
