#pragma once
#include "core/mode.h"
#include <cstddef>
#include <cstdint>

namespace rs7pubstate {
// A validated tuning vector determines the instrument's width. Unsupported guitar widths fail
// closed to six; bass always retains its stock four strings.
unsigned Count(size_t tuningCount, bool bass);
int Lowest(const int16_t tuning[8], unsigned count, bool bass);
}  // namespace rs7pubstate

// C-lite: the guitar carries its string count from its build (core/clite.h); there is no publication
// of the count at song setup any more and nothing to restore. What a 7/8-string song still has to do at
// song setup (S4) is the per-song detector: widen its active string count and lowest open note.
extern "C" {
// S4 (core/mode.cpp), game thread. Native 7-string mode only (g_rs7_native_publish). A 7/8-entry guitar tuning
// on a guitar that was built with that many strings: detector +13D8h = count and +11F8h = the lowest open MIDI
// over all strings; the count survives the SetTuning that S4 runs after it (Rs7Pitch_SongCount). Any other
// song (6 strings, bass, a guitar built with another count, detector without the native tail) is left at the
// stock width and logged.
__declspec(dllexport) void __cdecl Rs7Song_WidenDetector(void* session, void* detector, const Rs7Int16Vec* tuning);
}
