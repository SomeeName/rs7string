#include "core/publish_state.h"
#include "core/gsnative.h"
#include "core/pitch8.h"
#include "core/publish.h"
#include "core/publish_grid.h"
#ifndef RS7_PUBLISH_STATE_PURE
#include "log.h"
#include <intrin.h>
#endif

unsigned rs7pubstate::Count(size_t n, bool bass) {
    return bass ? 4 : n == 7 || n == 8 ? unsigned(n) : 6;
}

int rs7pubstate::Lowest(const int16_t t[8], unsigned n, bool bass) {
    const int* base = bass ? g_rs7_pitch_bbase : g_rs7_pitch_gbase;
    const unsigned limit = bass ? 4 : 8;

    if (!n || n > limit) {
        n = bass ? 4 : 6;
    }
    int low = base[0] + t[0];

    for (unsigned s = 1; s < n; ++s) {
        const int pitch = base[s] + t[s];

        if (pitch < low) {
            low = pitch;
        }
    }

    return low;
}

#ifndef RS7_PUBLISH_STATE_PURE
namespace {
template <class T>
T& At(uint8_t* p, unsigned off) {
    return *reinterpret_cast<T*>(p + off);
}
}  // namespace

// Lifecycle log line (capped). Exported so the emulator tests can stub it (no Win32 there).
extern "C" __declspec(dllexport) __declspec(noinline) void __cdecl Rs7Song_Note(const char* what, const void* info,
                                                                                unsigned a, unsigned b) {
    static volatile long lines = 0;

    if (_InterlockedIncrement(&lines) <= 200) {
        Rs7Log("song detector: %s (info %p, %u, %u)", what, info, a, b);
    }
}

void Rs7Song_WidenDetector(void* session, void* detector, const Rs7Int16Vec* v) {
    if (!g_rs7_native_publish || !session || !detector || !v) {
        return;
    }

    // Integer range checks avoid undefined pointer subtraction for malformed vectors.
    const uintptr_t begin = reinterpret_cast<uintptr_t>(v->begin), end = reinterpret_cast<uintptr_t>(v->end);
    if (!begin || end < begin || (end - begin) % sizeof(int16_t) || end - begin > 8 * sizeof(int16_t)) {
        return;
    }

    const size_t n = (end - begin) / sizeof(int16_t);
    if (n != 4 && n != 6 && n != 7 && n != 8) {
        return;
    }

    auto* det = static_cast<uint8_t*>(detector);
    const bool bass = At<int>(det, rs7::kDetInstrument) == 2;
    const unsigned count = rs7pubstate::Count(n, bass);

    Rs7Pitch_SetSongCount(det, 0);  // a previous song of this detector does not carry over
    if (count == 6 || bass) {
        return;  // the stock width: nothing to widen
    }

    auto* guitar = At<uint8_t*>(static_cast<uint8_t*>(session), rs7::kSessionGuitar);
    auto* info = guitar ? At<uint8_t*>(guitar, 0x24) : nullptr;

    if (!Rs7Gs_InfoNative(info)) {
        Rs7Song_Note("not widened: the guitar's InstrumentInfo is not native", info, count, 0);
        return;
    }

    // Review F3: GridBuild gave up on this guitar's cells 6/7 (5 s timeout): no neck geometry for the extra strings, so
    // the song stays at the stock width instead of scoring strings that cannot be drawn.
    if (Rs7Publish_NeckFailed(guitar)) {
        Rs7Song_Note("not widened: the neck of this guitar failed (GridBuild timed out without cells 6/7)", info, count,
                     0);
        return;
    }

    const unsigned built = At<uint8_t>(info, 0x0B);

    if (built != count) {
        Rs7Song_Note("not widened: the guitar was built with another string count (song, guitar)", info, count, built);
        return;
    }

    if (!Rs7Pitch_Tailed(det, 1)) {
        Rs7Song_Note("not widened: the detector has no native tail", info, count, 0);
        return;
    }

    int16_t tuning[8] = {};

    for (size_t i = 0; i < n; ++i) {
        tuning[i] = v->begin[i];
    }

    const int lowest = rs7pubstate::Lowest(tuning, count, false);

    Rs7Pitch_SetSongCount(det, count);
    At<uint32_t>(det, rs7p::kDetCount) = count;
    At<int>(det, rs7::kDetLowestOpen) = lowest;
    Rs7Song_Note("detector widened", info, count, unsigned(lowest));
}
#endif
