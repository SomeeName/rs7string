#include "core/settings.h"
#include "core/color.h"
#include "core/colournative.h"
#include "core/config.h"
#include "core/frets.h"
#include "core/headstock.h"
#include "core/rsmodscolour.h"
#include "core/songend.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "core/tuner.h"
#include "d3d/swap.h"
#include "hooks/registry.h"
#include "log.h"
#include <windows.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using T = Rs7SetType;
using W = Rs7SetWhen;

int Index(const char* id);
const Rs7SetValue& Val(const char* id);

// ---- apply functions (Live / NextSession). Each one writes what the hooks read; see settings.h.
void ApplyDiag(const Rs7SetValue& v) {
    g_rs7_diag = v.i ? 1 : 0;
}  // R1/R2 logger calls (D2/D3/D5: startup)

void ApplyAutotune(const Rs7SetValue& v) {
    g_rs7_tuner_auto = static_cast<uintptr_t>(v.i);
    // The tuner stubs only enter C++ while g_rs7_tuner_any is set; the tuner recomputes it at its
    // next event (UpdateAny), so set it now: any = 1 without a session is the autotune state.
    if (v.i) {
        _InterlockedExchange(&g_rs7_tuner_any, 1);
    }
}

void ApplyEndLog(const Rs7SetValue& v) {
    g_rs7_endlog = v.i ? 1 : 0;
}

void ApplyLowRange(const Rs7SetValue& v) {
    g_rs7_lowrange = v.i ? 1 : 0;
}  // W: read per check

void ApplyColour(const Rs7SetValue&) {
    uint32_t rgb[2] = {Val("color.string6_color").rgb, Val("color.string6_color_cb").rgb};

    rs7rsmods::Resolve(rs7rsmods::Cached(), Val("color.rsmods_extended_range").i != 0, rgb);

    for (int m = 0; m < 2; ++m) {
        float f[3];

        Rs7_RgbToFloats(rgb[m], f);

        for (int c = 0; c < 3; ++c) {
            g_rs7_fx_rgb[m * 3 + c] = f[c];  // native colour matrix, 7th string palette / glow, tuner
        }
    }

    Rs7Swap_RequestRecolour(rgb);  // the gem ramps: repainted by the render thread (swap.h)
    rs7colour::PublishCurrent();
}

void ApplyAtlasRow(const Rs7SetValue& v) {
    g_rs7_s7_v = v.f[0];
}  // N5 writes it every shown frame

void ApplyFrets(const Rs7SetValue& v) {
    g_rs7_frets_on = v.i ? 1 : 0;
}  // F2 restores / applies (frets.h)

void ApplyHalfLen(const Rs7SetValue& v) {
    g_rs7_fret_halflen = v.f[0];
}  // F2 re-applies on a change

void ApplyHead(const Rs7SetValue&) {
    Rs7Head_SetLive(Val("debug.head_cull").text, Val("debug.head_stretch").f[0], Val("debug.head_stretch_mode").i);
}

void ApplyNothing(const Rs7SetValue&) {}  // read by the overlay itself every frame

// The table: every key of config.h, window.h, headstock.h and the overlay's own.
const Rs7SetDef kDefs[] = {
    // [overlay]
    {"overlay.enabled", L"overlay", L"enabled", T::Bool, W::Restart, "1", 0, 1, "Overlay enabled",
     "Hooks Present/Reset of the game's device and the window procedure. 1 (default): hidden until the "
     "hotkey shows it (see Open at start). 0: none of it.",
     nullptr},
    {"overlay.hotkey", L"overlay", L"hotkey", T::Text, W::Restart, "F9", 0, 0, "Hotkey",
     "Key that shows/hides the overlay (F1-F24, INSERT, HOME, END, PAUSE, SCROLL, A-Z, 0-9, or 0xNN).", nullptr},
    {"overlay.open", L"overlay", L"open", T::Bool, W::Restart, "0", 0, 1, "Open at start",
     "1: the overlay is visible from the first frame.", nullptr},
    {"overlay.scale", L"overlay", L"scale", T::Float, W::Live, "0", 0, 4, "UI scale",
     "0 = automatic (window height / 720).", ApplyNothing},
    // [debug]
    {"debug.diagnostics", L"debug", L"diagnostics", T::Bool, W::Live, "0", 0, 1, "Diagnostics (D1-D5)",
     "Live: the R1/R2 loggers (D1/D4). D2/D3/D5 are written only when it is 1 at startup.", ApplyDiag},
    {"debug.tuner_autotune", L"debug", L"tuner_autotune", T::Int, W::Live, "0", 0, 30, "Tuner autotune (s)",
     "Test driving: each tuner string reports tuned N s after it was selected. 0 = off.", ApplyAutotune},
    {"debug.song_end_log", L"debug", L"song_end_log", T::Bool, W::Live, "1", 0, 1, "Song end log",
     "Log the song session's state changes and which route ends a song (SE1-SE3, issue #13).", ApplyEndLog},
    {"debug.head_cull", L"debug", L"head_cull", T::Text, W::Live, "", 0, 0, "Headstock cull node",
     "Keep this headstock frame node culled on the 7-string guitar (empty = off).", ApplyHead},
    {"debug.head_stretch", L"debug", L"head_stretch", T::Float, W::Live, "1", 0.5f, 2.0f, "Headstock stretch",
     "Length-only stretch of the headstock about the nut (1 = off).", ApplyHead},
    {"debug.head_stretch_mode", L"debug", L"head_stretch_mode", T::Int, W::Live, "0", 0, 1, "Stretch mode",
     "0 = rotate diag(1,k,1), 1 = also Entity_SetScale(k).", ApplyHead},
    // [color]
    {"color.rsmods_extended_range", L"color", L"rsmods_extended_range", T::Bool, W::Live, "1", 0, 1,
     "Follow RSMods Extended Range colour",
     "When RSMods is installed with ExtendedRange=on, use its low B colour in both colour modes. "
     "RSMods settings are read at startup. Turn off to use the two colours below; their saved values stay intact.",
     ApplyColour},
    {"color.string6_color", L"color", L"string6_color", T::Rgb, W::Live, "C8C8C8", 0, 0, "String 6 colour",
     "Gems (ramp copy), sparks, hit/miss text, 7th string line, glow, tuner.", ApplyColour},
    {"color.string6_color_cb", L"color", L"string6_color_cb", T::Rgb, W::Live, "C8C8C8", 0, 0,
     "String 6 colour (colourblind)", "The same in colourblind mode (default: string6_color).", ApplyColour},
    {"color.ramp_swap", L"color", L"ramp_swap", T::Bool, W::Restart, "1", 0, 1, "Ramp copy (Step 2)",
     "Copy the stock colour ramps with a string-6 band (D3D SetTexture/GetTexture hooks).", nullptr},
    {"color.allow_inexact", L"color", L"allow_inexact", T::Bool, W::Restart, "0", 0, 1, "Allow DXT ramps",
     "Also copy DXT1/DXT5 ramps (CPU-decoded, not bit-exact).", nullptr},
    // [neck]
    {"neck.string7_line", L"neck", L"string7_line", T::Bool, W::Restart, "1", 0, 1, "7th string line",
     "The 7th string line (native GuitarStrings); pools are sized at the guitar load.", nullptr},
    {"neck.string7_v", L"neck", L"string7_v", T::Float, W::Live, "0.4", 0, 0.9f, "7th string atlas row",
     "0.4..0.9 = guitar strings 0..5, 0.0..0.3 = bass strings.", ApplyAtlasRow},
    {"neck.fret_extend", L"neck", L"fret_extend", T::Bool, W::Live, "1", 0, 1, "Fret wires span the 7th string",
     "F1/F2; off restores the stock bars at the next frame.", ApplyFrets},
    {"neck.fret_halflen", L"neck", L"fret_halflen", T::Float, W::Live, "4.61", 1, 20, "Fret bar half length",
     "L in k = (L + d/2) / L; re-applied at the next frame.", ApplyHalfLen},
    {"neck.headstock_peg", L"neck", L"headstock_peg", T::Bool, W::Restart, "1", 0, 1, "7th tuning peg",
     "Peg 7 on the 7-string model's Locator_Peg_7 (native PegSet slot 6, or the legacy spare peg).", nullptr},
    {"chords.native_chordloops", L"chords", L"native_chordloops", T::Bool, W::Restart, "1", 0, 1,
     "Native chord techniques",
     "Eight-slot chord bends, sustain techniques and 7/8-string chord frames; requires native_strings. In-game "
     "acceptance pending.",
     nullptr},
    {"neck.eight", L"neck", L"eight", T::Bool, W::Restart, "0", 0, 1, "8-string preparation",
     "Experimental: chord columns 6/7 and virtual-guitar string 7. 8Str names follow [tuner] names; native "
     "publish/tuner and the eighth peg are separate work.",
     nullptr},
    // [psarc]
    {"psarc.plain", L"psarc", L"plain", T::Bool, W::Restart, "1", 0, 1, "Plain PSARC loading",
     "Unencrypted CDLC packages (plain TOC, SNG flags 1) load with any CDLC enabler. If the enabler already provides "
     "it, rs7string leaves its sites alone. 0: rs7string does not provide it.",
     nullptr},
    // [assets]
    {"assets.headstock7", L"assets", L"headstock7", T::Bool, W::Restart, "1", 0, 1, "7-string headstock model",
     "7-string guitars show the 7-string headstock (AL1 + N5): headstock7_file, else dir + suffix.", nullptr},
    {"assets.headstock7_file", L"assets", L"headstock7_file", T::Text, W::Restart, "rs7_inline7.nif", 0, 0,
     "7-string model file",
     "The model for the in-line right-hand frame; a relative name is taken from dir (default: our Facet). none = the "
     "edited-file rule only.",
     nullptr},
    {"assets.headstock7_file_lh", L"assets", L"headstock7_file_lh", T::Text, W::Restart, "rs7_inline7_lh.nif", 0, 0,
     "7-string model file (left-handed)",
     "The model for the in-line left-hand frame (left-handed profiles); a relative name is taken from dir (default: "
     "our Facet, mirrored). none = the edited-file rule only.",
     nullptr},
    {"assets.headstock7_wood", L"assets", L"headstock7_wood", T::Float, W::Restart, "0.6", 0.05f, 1, "Wood brightness",
     "Brightness of the 7-string model's wood colour texture (1 = as built), applied when it is loaded.", nullptr},
    {"assets.dir", L"assets", L"dir", T::Text, W::Restart, "rs7string\\headstock\\", 0, 0, "Edited models folder",
     "<dir>\\<stock base><suffix>.nif for models without headstock7_file. A relative path is taken from the DLL's "
     "folder (the game folder).",
     nullptr},
    {"assets.suffix", L"assets", L"suffix", T::Text, W::Restart, "_7inline", 0, 0, "Edited models suffix",
     "File name suffix of the edited models.", nullptr},
    // [tuner]
    {"tuner.lowrange", L"tuner", L"lowrange", T::Bool, W::Live, "0", 0, 1, "Low-range tuner",
     "7-string tuner: the wrong-instrument warning and the detector's lowest open follow the lowest string.",
     ApplyLowRange},
    {"tuner.names", L"tuner", L"names", T::Bool, W::Restart, "1", 0, 1, "7-string tuning names",
     "Song list, song pages and tuner name 7-string arrangements (Rs7String6) as B Standard, Drop A, ...", nullptr},
    {"tuner.name_prefix", L"tuner", L"name_prefix", T::Text, W::Restart, "", 0, 31, "Name prefix",
     "Text before a 7-string tuning name (e.g. 7Str -> 7Str B Standard). Empty: none.", nullptr},
    {"tuner.name_suffix", L"tuner", L"name_suffix", T::Text, W::Restart, "7Str", 0, 31, "Name suffix",
     "Text after a 7-string tuning name (default 7Str -> B Standard 7Str). Empty: none.", nullptr},
    // [window]
    {"window.background", L"window", L"background", T::Bool, W::Restart, "0", 0, 1, "Background driving",
     "The game window never activates itself; injected keys via \\\\.\\pipe\\rs7string.", nullptr},
    {"window.screen", L"window", L"screen", T::Int, W::Restart, "0", 0, 16, "Screen",
     "Centre the game window on monitor N (0 = stock placement).", nullptr},
    {"window.console_quickedit_off", L"window", L"console_quickedit_off", T::Bool, W::Restart, "1", 0, 1,
     "Console QuickEdit off",
     "A click into the CDLC enabler's console no longer starts a selection that blocks console writes.", nullptr},
};
constexpr int kCount = int(sizeof kDefs / sizeof kDefs[0]);
Rs7SetState g_state[kCount];

int Index(const char* id) {
    for (int i = 0; i < kCount; ++i) {
        if (!std::strcmp(kDefs[i].id, id)) {
            return i;
        }
    }

    return -1;
}

const Rs7SetValue& Val(const char* id) {
    static const Rs7SetValue none{};
    const int i = Index(id);

    return i < 0 ? none : g_state[i].value;
}

const char* SkipBlanks(const char* p) {
    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    return p;
}

bool OnlyBlanks(const char* p) {
    return !*SkipBlanks(p);
}

bool ParseFloat(const char* text, double* out) {
    text = SkipBlanks(text);
    if (!*text) {
        return false;
    }

    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || !OnlyBlanks(end) || !std::isfinite(v)) {
        return false;
    }

    *out = v;

    return true;
}

bool ParseInt(const char* text, long* out) {
    text = SkipBlanks(text);
    if (!*text) {
        return false;
    }

    char* end = nullptr;

    errno = 0;
    const long v = std::strtol(text, &end, 10);
    if (end == text || !OnlyBlanks(end) || errno == ERANGE) {
        return false;
    }

    *out = v;

    return true;
}

bool InRange(const Rs7SetDef& d, double v) {
    return v >= double(d.lo) - 1e-6 && v <= double(d.hi) + 1e-6;
}

// Ini-text -> value (strict: the whole text, in range). False: out untouched.
bool Parse(const Rs7SetDef& d, const char* text, Rs7SetValue* out) {
    Rs7SetValue v{};

    switch (d.type) {
        case T::Bool: {
            long n = 0;
            if (!ParseInt(text, &n) || (n != 0 && n != 1)) {
                return false;
            }

            v.i = int(n);
            break;
        }
        case T::Int: {
            long n = 0;
            if (!ParseInt(text, &n) || !InRange(d, double(n))) {
                return false;
            }

            v.i = int(n);
            break;
        }
        case T::Float: {
            double f = 0;
            if (!ParseFloat(text, &f) || !InRange(d, f)) {
                return false;
            }

            v.f[0] = float(f);
            break;
        }
        case T::Rgb: {
            wchar_t w[32] = {};
            if (std::strlen(text) >= 31) {
                return false;
            }

            for (int k = 0; text[k]; ++k) {
                w[k] = wchar_t(static_cast<unsigned char>(text[k]));
            }

            if (!Rs7_ParseRgb(w, &v.rgb)) {
                return false;
            }

            break;
        }
        case T::Text: {
            const char* b = SkipBlanks(text);
            size_t n = std::strlen(b);

            while (n && (b[n - 1] == ' ' || b[n - 1] == '\t')) {
                --n;
            }

            if (n >= size_t(kRs7SetTextMax) || (d.hi > 0 && double(n) > double(d.hi))) {
                return false;  // hi: max length
            }

            for (size_t k = 0; k < n; ++k) {
                if (static_cast<unsigned char>(b[k]) < 0x20 || static_cast<unsigned char>(b[k]) > 0x7E) {
                    return false;
                }
            }

            std::memcpy(v.text, b, n);
            v.text[n] = 0;
            break;
        }
    }

    *out = v;

    return true;
}

bool Valid(const Rs7SetDef& d, const Rs7SetValue& v) {
    switch (d.type) {
        case T::Bool:
            return v.i == 0 || v.i == 1;
        case T::Int:
            return InRange(d, double(v.i));
        case T::Float:
            return std::isfinite(v.f[0]) && InRange(d, v.f[0]);
        case T::Rgb:
            return v.rgb <= 0xFFFFFF;
        case T::Text: {  // the same bounds as Parse(Text): terminated, ASCII, max length (d.hi)
            const void* z = std::memchr(v.text, 0, sizeof v.text);
            if (!z) {
                return false;
            }

            const size_t n = size_t(static_cast<const char*>(z) - v.text);
            if (n >= size_t(kRs7SetTextMax) || (d.hi > 0 && double(n) > double(d.hi))) {
                return false;
            }

            for (size_t k = 0; k < n; ++k) {
                if (static_cast<unsigned char>(v.text[k]) < 0x20 || static_cast<unsigned char>(v.text[k]) > 0x7E) {
                    return false;
                }
            }

            return true;
        }
    }

    return false;
}

Rs7SetValue Default(const Rs7SetDef& d) {
    Rs7SetValue v{};

    Parse(d, d.def, &v);

    return v;
}

std::string Narrow(const std::wstring& w) {
    std::string s;

    for (wchar_t c : w) {
        s.push_back(c >= 0x20 && c < 0x7F ? char(c) : '?');
    }

    return s;
}

// The ini's text of a key ("" = missing), and whether it was there.
bool ReadIni(const Rs7SetDef& d, const wchar_t* ini, std::string* out) {
    wchar_t buf[kRs7SetTextMax + 8] = {};
    const DWORD n = GetPrivateProfileStringW(d.section, d.key, L"\x01", buf, DWORD(sizeof buf / sizeof buf[0]), ini);
    if (n == 1 && buf[0] == 1) {
        return false;
    }

    *out = Narrow(std::wstring(buf, n));

    return true;
}
}  // namespace

bool Rs7Set_Equal(Rs7SetType t, const Rs7SetValue& a, const Rs7SetValue& b) {
    switch (t) {
        case T::Bool:
        case T::Int:
            return a.i == b.i;
        case T::Float:
            return a.f[0] == b.f[0];
        case T::Rgb:
            return a.rgb == b.rgb;
        case T::Text:
            return std::strcmp(a.text, b.text) == 0;
    }

    return false;
}

int Rs7Set_Count() {
    return kCount;
}

const Rs7SetDef& Rs7Set_Def(int i) {
    return kDefs[i];
}

Rs7SetState& Rs7Set_State(int i) {
    return g_state[i];
}

int Rs7Set_Find(const char* id) {
    if (!id) {
        return -1;
    }

    const int exact = Index(id);
    if (exact >= 0 || std::strchr(id, '.')) {
        return exact;
    }

    int found = -1;

    for (int i = 0; i < kCount; ++i) {
        const char* bare = std::strchr(kDefs[i].id, '.') + 1;

        if (!std::strcmp(bare, id)) {
            if (found >= 0) {
                return -1;  // ambiguous
            }

            found = i;
        }
    }

    return found;
}

void Rs7Set_ResetForTests() {
    for (int i = 0; i < kCount; ++i) {
        g_state[i] = Rs7SetState{};
        g_state[i].value = g_state[i].running = g_state[i].saved = Default(kDefs[i]);
    }
}

void Rs7Set_InitFromIni(const wchar_t* iniPath) {
    Rs7Set_ResetForTests();
    const std::wstring path = Rs7Config_ResolveIniPath(iniPath);

    if (path.empty()) {
        Rs7Log("config: invalid/overlong settings ini path; defaults used");
        return;
    }

    iniPath = path.c_str();
    // The keys of config.h through its loader: the same validation and the same defaults as the game uses.
    const Rs7Config c = Rs7Config_Load(iniPath);

    for (int i = 0; i < kCount; ++i) {
        const Rs7SetDef& d = kDefs[i];
        Rs7SetState& s = g_state[i];
        std::string text;

        s.fromIni = ReadIni(d, iniPath, &text);
        Rs7SetValue v = Default(d);

        if (s.fromIni && !Parse(d, text.c_str(), &v)) {
            if (d.type == T::Bool || d.type == T::Int) {
                v.i = 0;
                Rs7Log("config: [%ls] %ls invalid integer; disabled", d.section, d.key);
            } else {
                v = Default(d);
            }
        }

        if (!std::strcmp(d.id, "color.string6_color")) {
            v.rgb = c.string6Rgb;
        }
        if (!std::strcmp(d.id, "color.string6_color_cb")) {
            v.rgb = c.string6RgbCb;
        }
        if (!std::strcmp(d.id, "neck.string7_v")) {
            v.f[0] = c.string7V;
        }
        if (!std::strcmp(d.id, "neck.fret_halflen")) {
            v.f[0] = c.fretHalfLen;
        }
        if (!std::strcmp(d.id, "debug.tuner_autotune")) {
            v.i = c.tunerAutotune;
        }
        s.value = s.running = s.saved = v;
    }
}

const char* Rs7Set_ResultName(Rs7SetResult r) {
    switch (r) {
        case Rs7SetResult::Live:
            return "live";
        case Rs7SetResult::NextSession:
            return "next session";
        case Rs7SetResult::Restart:
            return "restart required";
        case Rs7SetResult::Unchanged:
            return "unchanged";
        case Rs7SetResult::Invalid:
            return "invalid value";
        case Rs7SetResult::Unknown:
            return "unknown key";
    }

    return "?";
}

Rs7SetResult Rs7Set_Commit(int i, const Rs7SetValue& v) {
    if (i < 0 || i >= kCount) {
        return Rs7SetResult::Unknown;
    }

    const Rs7SetDef& d = kDefs[i];
    if (!Valid(d, v)) {
        return Rs7SetResult::Invalid;
    }

    Rs7SetState& s = g_state[i];
    if (Rs7Set_Equal(d.type, s.value, v) && (d.when == W::Restart || Rs7Set_Equal(d.type, s.running, v))) {
        return Rs7SetResult::Unchanged;
    }

    s.value = v;
    if (d.when == W::Restart) {
        return Rs7SetResult::Restart;
    }

    s.running = v;
    if (d.apply) {
        d.apply(v);
    }

    return d.when == W::Live ? Rs7SetResult::Live : Rs7SetResult::NextSession;
}

Rs7SetResult Rs7Set_SetText(const char* id, const char* text) {
    const int i = Rs7Set_Find(id);
    if (i < 0) {
        return Rs7SetResult::Unknown;
    }

    Rs7SetValue v{};
    if (!text || !Parse(kDefs[i], text, &v)) {
        return Rs7SetResult::Invalid;
    }

    return Rs7Set_Commit(i, v);
}

std::string Rs7Set_Format(int i, const Rs7SetValue& v) {
    char b[kRs7SetTextMax + 8];

    switch (kDefs[i].type) {
        case T::Bool:
        case T::Int:
            std::snprintf(b, sizeof b, "%d", v.i);
            break;
        case T::Float:
            std::snprintf(b, sizeof b, "%.4g", double(v.f[0]));
            break;
        case T::Rgb:
            std::snprintf(b, sizeof b, "%06X", unsigned(v.rgb));
            break;
        case T::Text:
            std::snprintf(b, sizeof b, "%s", v.text);
            break;
    }

    return b;
}

bool Rs7Set_Dirty(int i) {
    return !Rs7Set_Equal(kDefs[i].type, g_state[i].value, g_state[i].saved);
}

int Rs7Set_DirtyCount() {
    int n = 0;

    for (int i = 0; i < kCount; ++i) {
        n += Rs7Set_Dirty(i) ? 1 : 0;
    }

    return n;
}

bool Rs7Set_NeedsRestart(int i) {
    return kDefs[i].when == W::Restart && !Rs7Set_Equal(kDefs[i].type, g_state[i].value, g_state[i].running);
}

int Rs7Set_Save(const wchar_t* iniPath) {
    int written = 0;

    for (int i = 0; i < kCount; ++i) {
        if (!Rs7Set_Dirty(i)) {
            continue;
        }

        const std::string text = Rs7Set_Format(i, g_state[i].value);
        const std::wstring w(text.begin(), text.end());

        if (!WritePrivateProfileStringW(kDefs[i].section, kDefs[i].key, w.c_str(), iniPath)) {
            Rs7Log("overlay: saving [%ls] %ls failed (%lu)", kDefs[i].section, kDefs[i].key, GetLastError());
            return -1;
        }

        g_state[i].saved = g_state[i].value;
        g_state[i].fromIni = true;
        ++written;
    }

    return written;
}

bool Rs7Set_Bool(const char* id) {
    return Val(id).i != 0;
}

int Rs7Set_Int(const char* id) {
    return Val(id).i;
}

float Rs7Set_Float(const char* id) {
    return Val(id).f[0];
}

const char* Rs7Set_Text(const char* id) {
    return Val(id).text;
}
