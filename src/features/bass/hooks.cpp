// Extended bass glue (bass.h): the song's mode from its RS7X block, bound to the detector at song setup (S4). The
// pitch, detector and lane callbacks of the pitch, publish and string7 features read it.
#include "features/bass/bass.h"
#include "features/colours/colours.h"
#include "features/nslot/nslot.h"
#include "model/instrument.h"
#include "platform/log.h"
#include <windows.h>

extern "C" {
__declspec(dllexport) volatile uint32_t g_rs7_bass_strings = 0;
__declspec(dllexport) rs7bass::Players g_rs7_bass_players = {};
}

namespace {
constexpr uintptr_t kSongTuning = 0x110;  // SNG song: std::vector<int16> tuning (S4 passes song + 110h)

// After a player's setup: the song-wide values. The low B shows string 6's grey through colour slot 4 (SetBassLowB),
// the slot a guitar's B string uses, so with a guitar player in the song it keeps slot 4's own colour.
void Publish() noexcept {
    const uint32_t strings = rs7bass::MaxStrings(g_rs7_bass_players);

    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_rs7_bass_strings), LONG(strings));
    rs7colour::SetBassLowB(strings >= 5 && !rs7bass::AnyGuitar(g_rs7_bass_players));
}
}  // namespace

uint32_t Rs7Bass_OnSong(const void* det, const void* tuning, int detInstrument) noexcept {
    // No game memory is read here: Rs7Ext_Find looks the song pointer up in the RS7X block store.
    const void* song = reinterpret_cast<const uint8_t*>(tuning) - kSongTuning;
    const auto block = rs7::IsBassKind(detInstrument) ? Rs7Ext_Find(song) : nullptr;
    const uint32_t strings =
        block ? rs7bass::SongStrings(true, block->instrument, block->stringCount, detInstrument) : 0;

    rs7bass::SetupPlayer(g_rs7_bass_players, det, detInstrument, strings, GetTickCount());
    Publish();

    if (strings || rs7::IsBassKind(detInstrument)) {
        try {
            Rs7Log("bass: song %p on detector %p: %s", song, det,
                   strings == 6   ? "6-string extended bass (low B, high C)"
                   : strings == 5 ? "5-string extended bass (low B)"
                                  : "stock 4-string bass");
        } catch (...) {
            // Diagnostic allocation failures must not interrupt song setup.
        }
    }

    return strings;
}

uint32_t Rs7Bass_DetectorStrings(const void* det) noexcept {
    return rs7bass::StringsOf(g_rs7_bass_players, det);
}
