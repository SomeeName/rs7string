// The keys removed in 1.0 (settings_removed.h) and their startup read.
#include "settings_removed.h"
#include "platform/config.h"
#include "platform/log.h"
#include "platform/settings.h"
#include <string>

namespace {
int g_removed = 0;

void ReadRemoved(const wchar_t* iniPath) {
    g_removed = Rs7Removed_ReadIni(iniPath);
}
}  // namespace

const Rs7SetTable kRs7RemovedSettings = {nullptr, 0, ReadRemoved};

const Rs7RemovedKey kRs7RemovedKeys[] = {
    {L"debug", L"string6_as_string0"},  // diagnostic mirror of string 0 (lanes and colours)
    {L"neck", L"native_colours"},       // 0: the stock six-column colours with C1-C5 / N11 / GSCL
    {L"neck", L"native_tuner"},         // 0: the six-slot tuner with the low B as a phase of slot 0
    {L"tuner", L"seven_slot"},          // 0: the stock six-string tuner (also turned the native tuner off)
    {L"debug", L"tuner_trace"},         // logged the B-phase tuner's string selections
    {L"neck", L"native_pegset"},        // 0: the stage-2 7th peg mirrored from peg 1 (PG1-PG4, PH1/PH2)
    {L"neck", L"peg7_k"},               // that peg's spacing factor
    {L"neck", L"peg7_offset"},          // that peg's position offset
    {L"neck", L"native_strings"},       // 0: the DLL-owned 7th string object S7 / S7G (N2-N4, N6, N7, N14-N17)
    {L"neck", L"string7_draw_fix"},     // N10 added S7's mesh to the noteway draw list
    {L"debug", L"string7_gpu_diag"},    // observed S7's draw calls on the device (d3d/gpudiag)
    {L"neck", L"native_nextgrid"},      // 0: the stock six-row grid with R7b / N9 for string 6
    {L"neck", L"native_fretgrid"},      // 0: the stock six-string FretGrid
    {L"neck", L"native_pitch"},         // 0: the stock detector / matcher storage with the string-6 side store
    {L"neck", L"native_publish"},       // 0: the older per-song 7-string mode
    {L"neck", L"clite"},                // 0: the older per-song 7-string mode (no guitar rebuild)
    {L"neck", L"eight"},                // 0: eight-string charts played as seven (eight strings are always on)
    {L"neck", L"string7_v"},            // the 7th string's atlas row (always string 0's row, 0.4)
    {L"neck", L"fret_extend"},          // 0: stock fret bars on 7/8-string guitars
    {L"neck", L"fret_halflen"},         // the fret bars' half length (always 4.61)
    {L"neck", L"headstock_peg"},        // 0: six pegs on the 7-string headstock
    {L"color", L"ramp_swap"},           // 0: no ramp copy, the 7th string's gems in the preview row's colour
    {L"color", L"allow_inexact"},       // 1: also copied DXT1/DXT5 ramps (CPU-decoded, not bit-exact)
    {L"chords", L"frame_scale"},        // the old fixed chord-frame scale (ignored since the lane-based frames)
    {L"chords", L"frame_axis"},         // the chord frame's string axis (always y)
    {L"chords", L"native_chordloops"},  // 0: the 7-slot chord loops (N22 N23 NF2 NF3 CT4-CT11)
    {L"debug", L"head_stretch"},  // the headstock length stretch experiment (the frame rotation Rs7Head_Width writes)
    {L"debug", L"head_stretch_mode"},  // that experiment's Entity_SetScale mode
};
const int kRs7RemovedKeyCount = int(sizeof(kRs7RemovedKeys) / sizeof(kRs7RemovedKeys[0]));

int Rs7Removed_ReadIni(const wchar_t* iniPath) {
    int n = 0;
    if (!iniPath) {
        return n;
    }

    for (int i = 0; i < kRs7RemovedKeyCount; ++i) {
        std::wstring value;
        if (!Rs7Config_ReadPresent(kRs7RemovedKeys[i].section, kRs7RemovedKeys[i].key, iniPath, &value)) {
            continue;
        }

        ++n;
        Rs7Log("config: [%ls] %ls ignored, removed in 1.0", kRs7RemovedKeys[i].section, kRs7RemovedKeys[i].key);
    }

    return n;
}

int Rs7Removed_Count() {
    return g_removed;
}
