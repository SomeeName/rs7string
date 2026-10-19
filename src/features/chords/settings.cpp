// The chord feature's settings: [chords] native_chordloops (overlay and template) and its startup read. The other
// [chords] keys are read by hooks.cpp (frame_axis, wide_misses) and features/diagrams (diagrams7).
#include "platform/settings.h"
#include "features/chords/chords.h"
#include "platform/config.h"

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

Rs7ChordConfig g_config;

void Read(const wchar_t* iniPath) {
    g_config = Rs7Chord_ReadIni(iniPath);
}

const Rs7SetDef kDefs[] = {
    {"chords.native_chordloops", L"chords", L"native_chordloops", T::Bool, W::Restart, "1", 0, 1,
     "Native chord techniques",
     "Eight-slot chord bends, sustain techniques and 7/8-string chord frames; requires native_strings. In-game "
     "acceptance pending.",
     nullptr},
};
}  // namespace

const Rs7SetTable kRs7ChordSettings = {kDefs, int(sizeof kDefs / sizeof kDefs[0]), Read};

const Rs7ChordConfig& Rs7Chord_Config() {
    return g_config;
}

Rs7ChordConfig Rs7Chord_ReadIni(const wchar_t* iniPath) {
    Rs7ChordConfig c;
    if (!iniPath) {
        return c;
    }

    c.nativeChordloopsRaw = Rs7Config_ReadInt(L"chords", L"native_chordloops", 1, iniPath);
    c.nativeChordloops = c.nativeChordloopsRaw != 0;

    return c;
}
