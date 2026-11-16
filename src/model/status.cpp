#include "model/status.h"
#include "model/slots.h"
#include "model/tuning.h"
#include <windows.h>
#include <cstdio>

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

void Rs7Status_SetSong(int entries, int strings, bool bass, bool active, const int16_t* tuning) {
    AcquireSRWLockExclusive(&g_lock);
    g_song.songs += 1;
    g_song.entries = entries;
    g_song.strings = strings;
    g_song.bass = bass;
    g_song.active = active;
    g_song.t6 = active && tuning && entries > 6 ? tuning[6] : 0;  // tuning holds `entries` values

    for (int i = 0; i < int(sizeof g_song.tuning / sizeof g_song.tuning[0]); ++i) {
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

int Rs7Status_SongRows(const Rs7SongStatus& s, Rs7StatusRow* rows, int max) {
    int n = 0;
    auto add = [&](const char* label, bool on, const char* fmt, auto... args) {
        if (n < max) {
            rows[n].label = label;
            rows[n].on = on;
            std::snprintf(rows[n].text, sizeof rows[n].text, fmt, args...);
            ++n;
        }
    };

    if (!s.songs) {
        add("last song setup", false, "%s", "none yet");
        return n;
    }

    const bool extra = unsigned(s.strings) > rs7::StockStrings(s.bass);

    if (s.active) {
        add("extra strings", true, "ON (low B = MIDI %d, t6 = %d)", rs7::kLowB + s.t6, s.t6);
    } else {
        add("extra strings", extra, "%s", extra ? "ON" : "off");
    }
    add("strings", false, "%d (%d SNG tuning entries%s)", s.strings, s.entries, s.bass ? ", bass" : "");
    char tun[64] = {};
    size_t len = 0;

    for (int k = 0; k < s.entries && k < int(sizeof s.tuning / sizeof s.tuning[0]) && len + 8 < sizeof tun; ++k) {
        len += size_t(std::snprintf(tun + len, sizeof tun - len, "%s%d", k ? " " : "", int(s.tuning[k])));
    }

    add("tuning offsets", false, "%s", tun);
    add("song setups", false, "%ld this run", s.songs);

    return n;
}
