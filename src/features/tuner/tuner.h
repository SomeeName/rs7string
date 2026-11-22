#pragma once
#include "model/instrument.h"
#include <cstdint>
#include <string>

// The tuner for 7/8-string guitar and 5/6-string bass arrangements (docs/ARCHITECTURE.md section 8; GitHub issues #1,
// #8, #15, #18).
// The public header: the only one code outside src/features/tuner/ includes.
//
// Three parts, one feature:
//  - The shared tuner hooks (hooks.cpp): TK reads the manifest keys RsxrString6 / RsxrString7 per offset array (the
//    pre-song tuner opens before song setup, so these keys are the only early source), TS hands the session's target
//    (6, 7 or 8 strings) to the native session, W the wrong-instrument threshold ([tuner] lowrange), AT
//    [debug] tuner_autotune.
//  - The native 8-slot tuner session (hooks_session.cpp, hooks_methods.cpp, hooks_copies.cpp): a 7/8-string session
//    runs relocated copies of the UI_PlayerTuner methods over a capacity-8 tail (TU8* / TS8* sites).
//  - Extended tuning names (tuningname.cpp logic, hooks_names.cpp glue; TJ TU TUT TN sites).
//
// Files: tuner.cpp / tuningname.cpp (logic, tests/tuner), hooks*.cpp (glue; hooks.h declares it, state.h holds the
// feature's state), hooks_entries.cpp (entry dispatch, session storage and AT mid-hooks),
// hooks_callin.cpp (the calls written into the copies), registration.cpp (the two hook tables). The ClipFade refs the
// native strings and pegs use are shared and live in features/scene/scene.h. settings.cpp: the [tuner] and [debug]
// tuner_autotune keys and their startup read.

struct RsxrSetTable;

extern "C" {
// The native 8-slot tuner session, a member of the native 7-string unit since 1.0 (no ini switch): set by Rsxr_Startup,
// cleared only when a startup check refuses it (then nothing is patched) or the unit loses a prerequisite.
__declspec(dllexport) extern uintptr_t g_rsxr_native_tuner;
// [debug] tuner_autotune seconds (0 = off).
__declspec(dllexport) extern uintptr_t g_rsxr_tuner_auto;
// Non-zero while some native tuner session exists or autotune is on: the stubs' fast stock path.
__declspec(dllexport) extern volatile long g_rsxr_tuner_any;
// [tuner] lowrange.
__declspec(dllexport) extern uintptr_t g_rsxr_lowrange;
}

// ---- settings (settings.cpp)

// [debug] tuner_autotune; [tuner] lowrange, names, name_prefix, name_suffix.
extern const RsxrSetTable kRsxrTunerSettings;
// The 7-string tuner status row and the Tuner tab (platform/uilayout.h).
struct RsxrUiTable;
extern const RsxrUiTable kRsxrTunerUi;

// [tuner] and [debug] tuner_autotune as read at startup.
//   lowrange=0|1             1 (default): 7-string tuner sessions (RsxrString6) lower the guitar
//                            wrong-instrument threshold (mean <= 34) to the arrangement's lowest open
//                            note - 1 and the detector's lowest-open to that note, so Drop A / A / G
//                            Standard do not warn (hooks.h). 0: stock 34. 6-string and bass: always stock.
//   names=0|1                1 (default): 7-string arrangements (manifest key RsxrString6) show a
//                            7-string tuning name in the song list, the song/arrangement pages and
//                            the tuner (TJ/TU/TUT/TN, tuningname.h). 0: the stock names.
//   name_prefix=             text before a 7-string name (default empty), e.g. "7Str" -> "7Str B Standard".
//   name_suffix=7Str         text after a 7-string name (default "7Str": "B Standard 7Str"); an empty
//                            value (name_suffix=) = no suffix. ASCII, at most 31 characters each
//                            (longer or non-ASCII text: the default, logged).
//   [debug] tuner_autotune=N test driving without a guitar: the tuner reports a string as tuned N
//                            seconds (1..30) after it was selected. 0 (default): off.
struct RsxrTunerConfig {
    int tunerLowRangeRaw = 1;
    bool tunerLowRange = true;  // [tuner] lowrange, raw != 0
    int tunerAutotune = 0;      // seconds, 0 = off (clamped to 0..30)
    int tunerNamesRaw = 1;
    bool tunerNames = true;           // raw != 0
    std::string namePrefix;           // [tuner] name_prefix (ASCII)
    std::string nameSuffix = "7Str";  // [tuner] name_suffix (ASCII)
    bool nameAffixValid = true;       // both valid (else the invalid one has its default)
};

// Reads [tuner] and [debug] tuner_autotune from a resolved ini path (nullptr: the defaults).
RsxrTunerConfig RsxrTuner_ReadIni(const wchar_t* iniPath);
// The last startup read (RsxrSet_ReadIni).
const RsxrTunerConfig& RsxrTuner_Config();
// [tuner] name_prefix / name_suffix text: printable ASCII, at most 31 characters (tuningname.cpp).
bool Rsxr_ValidNameAffix(const std::wstring& text);

// ---- startup (hooks.cpp, hooks_copies.cpp)

// Applies [tuner] lowrange / names / name_prefix / name_suffix and [debug] tuner_autotune (RsxrTuner_Config) and logs
// them. Before any site is patched.
void RsxrTuner_Configure();
// Builds the native session's copies from the live code; false = native_tuner off (logged).
bool RsxrTuner8_BuildCopies();
// After the native strings' copies: native_tuner needs the native strings, FretGrid and colours (their copies carry
// the in-place edits), then its own copies (RsxrTuner8_BuildCopies); either failing turns native_tuner off (logged).
void RsxrTuner8_Prepare();
// `skip` is [debug] skip_sites as ",ID,...,": a skipped TU8* / TS8* site turns native_tuner off (the grown session,
// its copies and mid-hooks are one contract). Logs the final state. Before any site is written.
void RsxrTuner8_ApplySkip(const std::string& skip);

// Overlay status: native tuner sessions now (those in the wrong-instrument table), g_rsxr_tuner_any.
struct RsxrTunerStats {
    int sessions;
    bool any;
};

void RsxrTuner_Stats(RsxrTunerStats* out);

// The feature's hooks (registration.cpp), consumed by src/registry.cpp: the native session (TU8* / TS8*, group
// Tuner) and the shared tuner hooks and tuning names (TK TS W AT TJ TU TUT TN and their refs, group Core: never
// isolated, a failure aborts patching like any core site).
struct RsxrHookTable;
extern const RsxrHookTable kRsxrTunerHooks;
extern const RsxrHookTable kRsxrTunerCoreHooks;

// ---- logic (tuner.cpp): values in, values out, no Win32, no game state

namespace rsxrtuner {
// The game's wrong-instrument threshold (MIDI mean, X_WTHR's 34.0).
constexpr int kStockWrongThreshold = 34;

// The guitar wrong-instrument threshold for a lowest open MIDI: min(34, lowest - 1).
int WrongThreshold(int lowestOpen);

// Colour slot 4 (a bass's low B, a guitar's B string) for a target read now: string 6's grey for an extended bass,
// unless a guitar's target was read within the last second (co-op reads both players' targets together).
constexpr bool BassLowB(bool extendedBass, bool guitarSeen, uint32_t sinceGuitarMs) {
    return extendedBass && (!guitarSeen || sinceGuitarMs > 1000);
}
}  // namespace rsxrtuner

namespace rsxrtu {
// The tuning order of n strings: the strings above 6 lowest first, then 0..5 (n <= 6: 0..n-1). An extended bass
// (`bass`, 5 / 6 strings): the low B (string 4) first, then E A D G, then the high C (string 5).
void Order(int n, uint8_t out[8], bool bass = false);
// One step from cur along order[0..n): the next / previous string, or -1.
int Step(const uint8_t* order, int n, int cur, int dir);

// Whether TS makes / keeps a session native: a 7/8-string target, an already native session, or a
// guitar built with more than six strings whatever the target (never the stock six-slot arrays on a 7-built guitar).
// An extended bass (`bass`, built with 5 / 6 strings) goes native too: only the native order starts on its low B.
constexpr bool GoesNative(int n, bool wasNative, unsigned guitarCount, bool bass = false) {
    return n > 6 || wasNative || guitarCount > 6 || (bass && rsxr::IsExtendedBass(guitarCount));
}

// Open MIDI of every slot from the six offsets and the extra two (guitar).
void Targets(const int* offs, const int extra[2], int open[8], int off[8]);
// The label text of an extra string from its open MIDI ("B", "F#", ...).
const char* NoteName(int midi);
}  // namespace rsxrtu
