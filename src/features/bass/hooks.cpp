// Extended bass glue (bass.h): the song's mode from its extension block, bound to the detector at song setup (S4). The
// pitch, detector and lane callbacks of the pitch, publish and string7 features read it.
#include "features/bass/bass.h"
#include "features/colours/colours.h"
#include "features/nslot/nslot.h"
#include "model/instrument.h"
#include "platform/log.h"
#include <windows.h>

extern "C" {
__declspec(dllexport) volatile uint32_t g_rsxr_bass_strings = 0;
__declspec(dllexport) rsxrbass::Players g_rsxr_bass_players = {};
}

namespace {
constexpr uintptr_t kSongTuning = 0x110;  // SNG song: std::vector<int16> tuning (S4 passes song + 110h)

// After a player's setup: the song-wide values. The low B shows string 6's grey through colour slot 4 (SetBassLowB),
// the slot a guitar's B string uses, so with a guitar player in the song it keeps slot 4's own colour.
void Publish() noexcept {
    const uint32_t strings = rsxrbass::MaxStrings(g_rsxr_bass_players);

    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_rsxr_bass_strings), LONG(strings));
    rsxrcolour::SetBassLowB(strings >= 5 && !rsxrbass::AnyGuitar(g_rsxr_bass_players));
}
}  // namespace

uint32_t RsxrBass_OnSong(const void* det, const void* tuning, int detInstrument) noexcept {
    // No game memory is read here: RsxrExt_Find looks the song pointer up in the extension block store.
    const void* song = reinterpret_cast<const uint8_t*>(tuning) - kSongTuning;
    const auto block = rsxr::IsBassKind(detInstrument) ? RsxrExt_Find(song) : nullptr;
    const uint32_t strings =
        block ? rsxrbass::SongStrings(true, block->instrument, block->stringCount, detInstrument) : 0;

    rsxrbass::SetupPlayer(g_rsxr_bass_players, det, detInstrument, strings, GetTickCount());
    Publish();

    if (strings || rsxr::IsBassKind(detInstrument)) {
        try {
            RsxrLog("bass: song %p on detector %p: %s", song, det,
                    strings == 6   ? "6-string extended bass (low B, high C)"
                    : strings == 5 ? "5-string extended bass (low B)"
                                   : "stock 4-string bass");
        } catch (...) {
            // Diagnostic allocation failures must not interrupt song setup.
        }
    }

    return strings;
}

uint32_t RsxrBass_DetectorStrings(const void* det) noexcept {
    return rsxrbass::StringsOf(g_rsxr_bass_players, det);
}
