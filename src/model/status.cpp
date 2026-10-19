#include "model/status.h"
#include <windows.h>

namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
Rs7StartStatus g_start;
Rs7SongStatus g_song;
}  // namespace

void Rs7Status_SetStart(const Rs7StartStatus& s) {
    AcquireSRWLockExclusive(&g_lock);
    g_start = s;
    ReleaseSRWLockExclusive(&g_lock);
}

Rs7StartStatus Rs7Status_Start() {
    AcquireSRWLockShared(&g_lock);
    const Rs7StartStatus s = g_start;

    ReleaseSRWLockShared(&g_lock);

    return s;
}

void Rs7Status_SetSong(int entries, bool bass, bool active, const int16_t* tuning) {
    AcquireSRWLockExclusive(&g_lock);
    g_song.songs += 1;
    g_song.entries = entries;
    g_song.bass = bass;
    g_song.active = active;
    g_song.t6 = active && tuning ? tuning[6] : 0;

    for (int i = 0; i < 8; ++i) {
        g_song.tuning[i] = tuning && i < entries ? tuning[i] : 0;
    }

    ReleaseSRWLockExclusive(&g_lock);
}

Rs7SongStatus Rs7Status_Song() {
    AcquireSRWLockShared(&g_lock);
    const Rs7SongStatus s = g_song;

    ReleaseSRWLockShared(&g_lock);

    return s;
}
