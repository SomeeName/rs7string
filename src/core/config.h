#pragma once
#include "core/color.h"
#include <cstdint>
#include <string>

// rs7string.ini next to the DLL. A missing file or key means the defaults below.
//   [debug]
//   diagnostics=0|1          1: write the diagnostic sites D2/D3/D5 and let R2/R1 call the D1/D4
//                            loggers (rate-limited lines in rs7string.log). 0 (default): D2/D3/D5
//                            are still resolved (a moved pattern still patches nothing) but not
//                            written, and R2/R1 skip the logger calls.
//   [color]
//   string6_color=RRGGBB     string 6's colour (default C8C8C8; "#" prefix allowed). Invalid text
//                            (also any value of 255 or more characters, never truncated):
//                            the default, logged.
//   string6_color_cb=RRGGBB  the same in colourblind mode (default: string6_color; invalid text:
//                            string6_color, logged).
//   ramp_swap=0|1            1 (default): copy the stock ramp textures with a string-6 band (Step 2);
//                            0: Step 1 only, no D3D hooks at all.
//   allow_inexact=0|1        1: also swap DXT1/DXT5 ramps (CPU-decoded, GPU rounding may differ).
//   [neck]
//   string7_line=0|1         1 (default): the 7th string line on the neck (native strings, gsnative.h).
//                            0: N1 / N21 request the stock instance counts and N12 keeps string 6 out
//                            of the SNG section mask (the sites stay patched).
//   string7_v=0.4            the 7th string's atlas row (v_positionString / g_VPosition), 0.0..0.9:
//                            0.4..0.9 = guitar strings 0..5, 0.0..0.3 = bass strings. Default 0.4
//                            (string 0's row). Invalid text or out of range: the default, logged.
//   fret_extend=0|1          1 (default): on 7-string songs the 25 fret bars are moved half a string
//                            spacing towards the 7th string and scaled so they span it (F1/F2,
//                            frets.h). 0: F1 replays stock, the bars are never touched.
//   headstock_peg=0|1        1 (default): a 6-string guitar's native PegSet loads 7 pegs (PSL1,
//                            headstock.h); peg 7 sits on the 7-string model's Locator_Peg_7 ([assets]
//                            headstock7) and is active while the guitar shows the 7th string. A model
//                            without Locator_Peg_7 has no 7th peg. 0: 6 pegs (the layout stays native).
//   fret_halflen=4.61        the bar's half length L used for the scale k = (L + d/2) / L, 1..20.
//                            Invalid text or out of range: the default, logged.
//   [tuner]
//   lowrange=0|1             0 (default): opt in so 7-string tuner sessions (Rs7String6) lower the guitar
//                            wrong-instrument threshold (mean <= 34) to the arrangement's lowest open
//                            note - 1 and the detector's lowest-open to that note, so Drop A / A / G
//                            Standard do not warn (tuner.h). 0: stock 34. 6-string and bass: always stock.
//   names=0|1                1 (default): 7-string arrangements (manifest key Rs7String6) show a
//                            7-string tuning name in the song list, the song/arrangement pages and
//                            the tuner (TJ/TU/TUT/TN, tuningname.h). 0: the stock names.
//   name_prefix=             text before a 7-string name (default empty), e.g. "7Str" -> "7Str B Standard".
//   name_suffix=7Str         text after a 7-string name (default "7Str": "B Standard 7Str"); an empty
//                            value (name_suffix=) = no suffix. ASCII, at most 31 characters each
//                            (longer or non-ASCII text: the default, logged).
//   [debug]
//   tuner_autotune=N         test driving without a guitar: the tuner reports a string as tuned N
//                            seconds (1..30) after it was selected. 0 (default): off.
//   song_end_log=0|1         1 (default): log which route ends a song (issue #13, core/songend.h).
// Read once at startup. The overlay (core/settings.h) shows and edits every key above plus
//   [overlay] enabled=0|1, hotkey=F9, open=0|1, scale=0 (overlay/overlay.h), [window] background,
//   screen (core/window.h), [debug]
//   head_cull, head_stretch, head_stretch_mode (core/headstock.h);
// settings.cpp says which of them are live.
struct Rs7Config {
    bool found = false;        // the ini file exists
    int diagnosticsRaw = 0;    // the value as read
    bool diagnostics = false;  // raw != 0
    int removedKeys = 0;       // keys of kRs7RemovedKeys present in the file (each logged "ignored, removed in 1.0")

    bool colorValid = true;                   // string6_color missing, empty or valid
    uint32_t string6Rgb = kRs7DefaultColour;  // 0xRRGGBB
    std::wstring string6Raw;                  // the text as read (for the log)
    bool colorCbSet = false;                  // string6_color_cb present and non-empty
    bool colorCbValid = true;                 // string6_color_cb missing, empty or valid
    uint32_t string6RgbCb = kRs7DefaultColour;
    std::wstring string6CbRaw;
    int rampSwapRaw = 1;
    bool rampSwap = true;  // raw != 0
    int allowInexactRaw = 0;
    bool allowInexact = false;  // raw != 0
    int string7LineRaw = 1;
    bool string7Line = true;    // raw != 0
    std::wstring string7VRaw;   // the text as read (for the log)
    bool string7VValid = true;  // missing, empty or a number in [0, 0.9]
    float string7V = 0.4f;
    int headstockPegRaw = 1;
    bool headstockPeg = true;  // raw != 0
    // Offline eight-string preparation; does not publish a game string count.
    int eightRaw = 0;
    bool eight = false;  // [neck] eight, startup only
    int fretExtendRaw = 1;
    bool fretExtend = true;        // raw != 0
    std::wstring fretHalfLenRaw;   // the text as read (for the log)
    bool fretHalfLenValid = true;  // missing, empty or a number in [1, 20]
    float fretHalfLen = 4.61f;
    int tunerLowRangeRaw = 0;
    bool tunerLowRange = false;  // [tuner] lowrange, raw != 0
    int tunerAutotune = 0;       // seconds, 0 = off (clamped to 0..30)
    bool songEndLog = true;      // [debug] song_end_log
    int tunerNamesRaw = 1;
    bool tunerNames = true;           // raw != 0
    std::string namePrefix;           // [tuner] name_prefix (ASCII)
    std::string nameSuffix = "7Str";  // [tuner] name_suffix (ASCII)
    bool nameAffixValid = true;       // both valid (else the invalid one has its default)
    int nativeChordloopsRaw = 1;
    bool nativeChordloops = true;  // [chords] native_chordloops; startup only
};

// Keys of the legacy 7-string paths removed in 1.0. An old rs7string.ini that still has them loads: each one present is
// logged as "ignored, removed in 1.0" and changes nothing.
struct Rs7RemovedKey {
    const wchar_t* section;
    const wchar_t* key;
};

extern const Rs7RemovedKey kRs7RemovedKeys[];
extern const int kRs7RemovedKeyCount;

// [tuner] name_prefix / name_suffix text: printable ASCII, at most 31 characters.
bool Rs7_ValidNameAffix(const std::wstring& text);

// "0.35" -> 0.35f. Whole text must be a finite number in [0, 0.9] (blanks around it allowed).
bool Rs7_ParseAtlasRow(const wchar_t* text, float* out);

Rs7Config Rs7Config_Load(const wchar_t* iniPath);

// Profile APIs resolve bare filenames under Windows, while disk APIs use the current directory.
// Normalize once so all readers address the same file. Empty means invalid/overlong input.
std::wstring Rs7Config_ResolveIniPath(const wchar_t* iniPath);

// Missing keys use def; malformed/truncated/out-of-range values return zero and log.
int Rs7Config_ReadInt(const wchar_t* section, const wchar_t* key, int def, const wchar_t* iniPath, int hi = 1);
