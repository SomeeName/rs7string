#include "startup.h"
#include "core/allocstats.h"
#include "core/assetload.h"
#include "core/chord7.h"
#include "core/chordloops.h"
#include "core/clite.h"
#include "core/color.h"
#include "core/colournative.h"
#include "core/config.h"
#include "core/diagrams.h"
#include "core/eight.h"
#include "core/freeze.h"
#include "core/fretgrid.h"
#include "core/frets.h"
#include "core/gsnative.h"
#include "core/headstock.h"
#include "core/nextgrid.h"
#include "core/patch.h"
#include "core/pattern.h"
#include "core/pitch8.h"
#include "core/plainpsarc.h"
#include "core/protect.h"
#include "core/publish.h"
#include "core/publish_grid.h"
#include "core/publish_pitch.h"
#include "core/resolve.h"
#include "core/settings.h"
#include "core/sidestore.h"
#include "core/songend.h"
#include "core/spinprobe.h"
#include "core/status.h"
#include "core/string7.h"
#include "core/string7diag.h"
#include "core/tuner.h"
#include "core/tuner8.h"
#include "core/tuningname.h"
#include "core/vguit.h"
#include "d3d/present.h"
#include "d3d/worker.h"
#include "generated/sites.generated.h"
#include "hooks/isolate.h"
#include "hooks/registry.h"
#include "log.h"
#include "overlay/overlay.h"
#include "version.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const IMAGE_NT_HEADERS32* Nt(HMODULE exe) {
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);

    return reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(exe) + dos->e_lfanew);
}

bool TextSection(HMODULE exe, uintptr_t* va, size_t* size) {
    const auto nt = Nt(exe);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 6) == 0) {
            *va = reinterpret_cast<uintptr_t>(exe) + sec[i].VirtualAddress;
            *size = sec[i].Misc.VirtualSize;
            return true;
        }
    }

    return false;
}

// rs7string.ini next to this DLL.
std::wstring IniPath() {
    HMODULE self = nullptr;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&IniPath), &self);
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p(path, n);

    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"rs7string.ini";
}

Rs7Config LoadConfig() {
    const std::wstring ini = IniPath();
    const Rs7Config c = Rs7Config_Load(ini.c_str());

    g_rs7_eight = c.eight ? 1 : 0;  // before any module reads the preparation gate
    g_rs7_diag = c.diagnostics ? 1 : 0;
    // String 6's colour, [mode*3 + channel]: mode 0 normal, mode 1 colourblind.
    float rgb[3];

    Rs7_RgbToFloats(c.string6Rgb, rgb);
    std::copy(rgb, rgb + 3, g_rs7_fx_rgb);
    Rs7_RgbToFloats(c.string6RgbCb, rgb);
    std::copy(rgb, rgb + 3, g_rs7_fx_rgb + 3);
    Rs7Log("config %ls: %s", ini.c_str(), c.found ? "found" : "not found (defaults)");
    Rs7Log("config: [debug] diagnostics=%d -> diagnostics %s", c.diagnosticsRaw,
           c.diagnostics ? "ON (D1-D5 log lines; D2/D3/D5 patched)" : "off (D2/D3/D5 resolved, not patched)");
    Rs7Log("config: [color] string6_color=%06X%s string6_color_cb=%06X%s ramp_swap=%d allow_inexact=%d",
           unsigned(c.string6Rgb),
           c.colorValid ? (c.string6Raw.empty() ? " (default)" : "") : " (INVALID text, default used)",
           unsigned(c.string6RgbCb),
           !c.colorCbSet    ? " (= string6_color)"
           : c.colorCbValid ? ""
                            : " (INVALID text, string6_color used)",
           c.rampSwapRaw, c.allowInexactRaw);
    g_rs7_s7_on = c.string7Line ? 1 : 0;
    Rs7Log("config: [neck] string7_line=%d -> 7th string line %s", c.string7LineRaw,
           c.string7Line ? "ON" : "off (N1 / N21 request stock counts, N12 keeps string 6 out of the SNG mask)");
    g_rs7_s7_v = c.string7V;
    Rs7Log("config: [neck] string7_v=%.2f%s", double(c.string7V),
           c.string7VValid ? (c.string7VRaw.empty() ? " (default)" : "") : " (INVALID text, default used)");
    g_rs7_head_on = c.headstockPeg ? 1 : 0;
    g_rs7_native_pegset = 1;         // mandatory since 1.0
    g_rs7_native_strings = 1;        // mandatory since 1.0 (the copy check below may still refuse it)
    g_rs7_native_colours = 1;        // mandatory since 1.0 (the ColourManager check below may still refuse it)
    rs7fg::ConfigureDeferred(true);  // mandatory since 1.0
    g_rs7_native_pitch = 1;          // mandatory since 1.0 (needs MSVCR100's _msize, below)
    g_rs7_native_tuner = 1;          // mandatory since 1.0 (the checks after the site resolve may still refuse it)

    if (g_rs7_native_pitch) {
        // The detector / matcher come from MSVCR100's operator new: its _msize tells a grown block.
        HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");

        g_rs7_fn_msize = crt ? reinterpret_cast<uintptr_t>(GetProcAddress(crt, "_msize")) : 0;
        if (!g_rs7_fn_msize) {
            g_rs7_native_pitch = 0;
        }
    }

    if (!g_rs7_native_pitch) {
        Rs7Log("pitch8: MSVCR100 _msize not found - the native 8-entry tuning storage is unavailable");
    }
    rs7ng::Configure(true);  // mandatory since 1.0
    Rs7Head_SetIni(ini.c_str());
    Rs7Log("config: [neck] headstock_peg=%d -> 7th tuning peg %s", c.headstockPegRaw,
           !c.headstockPeg
               ? "off"
               : "native 7-slot PegSet: 6-string guitars load 7 pegs, peg 7 active on 7-string songs / tuners");
    Rs7Asset_Configure(ini.c_str());
    Rs7Alloc_Configure(ini.c_str());
    Rs7Chord_Configure(ini.c_str());
    Rs7Diagram_Configure(ini.c_str());

    {
        std::wstring csv = ini.substr(0, ini.find_last_of(L"\\/") + 1) + L"rs7string_vguitar.csv";

        Rs7Vg_Configure(ini.c_str(), csv.c_str());
    }

    g_rs7_frets_on = c.fretExtend ? 1 : 0;
    g_rs7_fret_halflen = c.fretHalfLen;
    Rs7Log("config: [neck] fret_extend=%d fret_halflen=%.3f%s -> fret bars %s", c.fretExtendRaw, double(c.fretHalfLen),
           c.fretHalfLenValid ? (c.fretHalfLenRaw.empty() ? " (default)" : "") : " (INVALID text, default used)",
           c.fretExtend ? "span the 7th string on 7-string songs (F1/F2)" : "stock (F1 replays stock)");
    if (!c.fretHalfLenValid) {
        Rs7Log("config: [neck] fret_halflen text was \"%ls\"", c.fretHalfLenRaw.c_str());
    }
    g_rs7_lowrange = c.tunerLowRange ? 1 : 0;
    Rs7Log("config: [tuner] lowrange=%d -> %s", c.tunerLowRangeRaw,
           c.tunerLowRange
               ? "7-string tuner: wrong-instrument threshold and detector lowest open follow the lowest string"
               : "stock (wrong-instrument threshold 34)");
    g_rs7_tuner_auto = static_cast<uintptr_t>(c.tunerAutotune);
    Rs7Tuner_ResetAll();
    Rs7Log("config: [debug] tuner_autotune=%d%s", c.tunerAutotune,
           c.tunerAutotune ? " s (TEST DRIVING: strings report tuned by themselves)" : " (off)");

    {
        rs7tn::Config tn;

        tn.on = c.tunerNames;
        if (c.eight) {
            Rs7Log("config: [neck] eight=1 -> 8-string preparation (T8 columns 6/7, vguitar string 7); native "
                   "publish/tuner acceptance still required");
        }
        std::snprintf(tn.prefix, sizeof tn.prefix, "%s", c.namePrefix.c_str());
        std::snprintf(tn.suffix, sizeof tn.suffix, "%s", c.nameSuffix.c_str());
        Rs7Tn_Configure(tn);
        g_rs7_tn_on = c.tunerNames ? 1 : 0;
        const int b0[7] = {0, 0, 0, 0, 0, 0, 0};

        Rs7Log("config: [tuner] names=%d name_prefix=\"%s\" name_suffix=\"%s\"%s -> %s", c.tunerNamesRaw, tn.prefix,
               tn.suffix, c.nameAffixValid ? "" : " (INVALID text replaced by the default)",
               c.tunerNames ? ("7-string tuning names, e.g. \"" + rs7tn::KnownDisplay(b0, tn, 0, false) + "\"").c_str()
                            : "stock tuning names");
    }

    g_rs7_endlog = c.songEndLog ? 1 : 0;
    Rs7Log("config: [debug] song_end_log=%d -> %s", int(c.songEndLog),
           c.songEndLog ? "song state changes and the route that ends a song are logged (SE1-SE3, issue #13)" : "off");
    if (!c.string7VValid) {
        Rs7Log("config: [neck] string7_v text was \"%ls\"", c.string7VRaw.c_str());
    }
    if (!c.colorValid) {
        Rs7Log("config: [color] string6_color text was \"%ls\"", c.string6Raw.c_str());
    }
    if (!c.colorCbValid) {
        Rs7Log("config: [color] string6_color_cb text was \"%ls\"", c.string6CbRaw.c_str());
    }

    return c;
}

// Runs inside a thread freeze (core/freeze.h): no allocation, no logging. A Bytes site's replacement is
// parsed beforehand (WriteBytesFor, outside the freeze).
bool Apply(const Rs7Resolved& r, const Rs7Hook& h, uintptr_t midEntry, const std::vector<uint8_t>& bytes) {
    const uintptr_t stub = h.callback ? midEntry : reinterpret_cast<uintptr_t>(h.stub);

    switch (r.site->kind) {
        case Rs7Kind::Jmp:
            return Rs7_WriteJmp(r.addr, static_cast<size_t>(r.site->length), stub);
        case Rs7Kind::JmpAtomic8:
            return Rs7_WriteJmpAtomic8(r.addr, stub);
        case Rs7Kind::Rel32:
            return Rs7_WriteRel32(r.addr, r.addr + 4, stub, true);
        case Rs7Kind::Call:
            return Rs7_WriteRel32(r.addr + 1, r.addr + 5, stub, false);
        case Rs7Kind::Bytes:
            if (bytes.empty() || bytes.size() != r.original.size()) {
                return false;
            }

            return Rs7_WriteBytes(r.addr, bytes.data(), bytes.size());
        case Rs7Kind::Ref:
        case Rs7Kind::CallRef:
            return true;  // never written (Rs7_ShouldPatch excludes them anyway)
    }

    return false;
}

// Outside the freeze: a Bytes site's replacement bytes (empty: not a Bytes site or unparsable).
std::vector<uint8_t> WriteBytesFor(const Rs7Resolved& r) {
    if (r.site->kind != Rs7Kind::Bytes) {
        return {};
    }

    Rs7Pattern w;
    if (!Rs7_ParsePattern(r.site->write, w) || w.bytes.size() != r.original.size()) {
        return {};
    }

    return w.bytes;
}

// The bytes a site's apply / restore touch: the original bytes cover every kind's write.
Rs7Range RangeOf(const Rs7Resolved& r) {
    return {r.addr, r.addr + r.original.size()};
}

// [neck] native_fretgrid=1: the FGR layout sites are written later, by the first call of the FGHK mid-hook (FGHK and
// FGCT, the mid-hooks, are written with the main batch) on the game's own thread (core/fretgrid.h,
// rs7fg::OnGuitarUpdate); the resolved sites wait here.
struct DeferredSite {
    Rs7Resolved r;
    const Rs7Hook* h;
    std::vector<uint8_t> bytes;  // parsed at startup (Bytes sites)
};

std::vector<DeferredSite> g_deferredFg;
std::vector<Rs7Range> g_deferredRanges;  // built at startup with g_deferredFg

struct DeferredRound {
    size_t n;           // sites written before the failure (== size: all)
    size_t unrestored;  // review #8: rollbacks that failed the read-back
    size_t firstBad;    // index of the first of them
};

void WriteDeferred(void* p) {  // inside the freeze
    DeferredRound& d = *static_cast<DeferredRound*>(p);
    size_t n = 0;

    for (; n < g_deferredFg.size(); ++n) {
        if (!Apply(g_deferredFg[n].r, *g_deferredFg[n].h, 0, g_deferredFg[n].bytes)) {
            break;
        }
    }

    d.n = n;
    if (n == g_deferredFg.size()) {
        return;
    }

    for (size_t j = 0; j < n; ++j) {
        if (!Rs7_RestoreBytes(g_deferredFg[j].r.addr, g_deferredFg[j].r.original.data(),
                              g_deferredFg[j].r.original.size()) &&
            d.unrestored++ == 0) {
            d.firstBad = j;
        }
    }
}

// The game (main) thread, first guitar update. The other threads (loaders, audio, our workers) are frozen
// while the FGR sites change (review #9).
bool ApplyDeferredFg() {
    DeferredRound d{0, 0, 0};
    Rs7FreezeStats st;
    const bool ran = Rs7_RunFrozen(g_deferredRanges.data(), g_deferredRanges.size(), &WriteDeferred, &d, &st);
    char line[256];

    Rs7_FreezeDescribe(st, line, sizeof line);
    Rs7Log("fretgrid: %s (%u deferred sites, game thread %lu)", line, unsigned(g_deferredFg.size()),
           GetCurrentThreadId());

    if (!ran) {
        Rs7_DisableFeature(Rs7Feature::Fretgrid);
        return false;
    }

    if (d.n == g_deferredFg.size()) {
        return true;
    }

    if (d.unrestored) {  // review #8: never retried (FGHK writes once); the group stays off
        Rs7Log("fretgrid: UNRECOVERABLE %s (+%u more): restoring the original bytes failed after %s could not be "
               "written - "
               "sites stay written, feature fretgrid DISABLED",
               g_deferredFg[d.firstBad].r.site->id, unsigned(d.unrestored - 1), g_deferredFg[d.n].r.site->id);
    } else {
        Rs7Log("fretgrid: writing %s failed - the %u sites already written were restored", g_deferredFg[d.n].r.site->id,
               unsigned(d.n));
    }

    // Only the Fretgrid group fails (and what depends on its layout: native_publish, native_tuner).
    Rs7_DisableFeature(Rs7Feature::Fretgrid);

    return false;
}

// The first member of the native 7-string unit whose switch is off, or nullptr when all are on.
const char* MissingSevenUnitMember() {
    if (!g_rs7_native_colours) {
        return "the native colours (8-column ColourManager)";
    }
    if (!g_rs7_native_pegset) {
        return "the native PegSet";
    }
    if (!g_rs7_native_strings) {
        return "the native GuitarStrings";
    }
    if (!g_rs7_native_fretgrid) {
        return "the native FretGrid";
    }
    if (!g_rs7_native_pitch) {
        return "the native pitch storage";
    }
    if (!g_rs7_native_nextgrid) {
        return "the native next-note grid";
    }
    if (!g_rs7_native_tuner) {
        return "the native tuner session";
    }
    if (!g_rs7_publish_ports || !g_rs7_native_publish) {
        return "the native publication ports";
    }
    if (!g_rs7_clite) {
        return "C-lite (the guitar build with 7/8 strings)";
    }

    return nullptr;
}

void Fail(const char* why) {
    Rs7StartStatus st;

    st.state = Rs7StartState::Failed;
    std::snprintf(st.error, sizeof st.error, "%s", why);
    Rs7Status_SetStart(st);
}
}  // namespace

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
// [debug] veh_log=1 (diagnostic): the first exceptions of every thread are logged by a vectored
// handler (it runs before the frame-based handlers and their chain check), then passed on.
volatile long g_vehLines = 0, g_vehGameLines = 0;  // own budget for faults in the game's image (the real crashes)

LONG CALLBACK VehLog(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C || code == 0x80000003 ||
        code == 0x80000004) {
        return EXCEPTION_CONTINUE_SEARCH;  // thread names, C++ throws, the packer's break / single steps
    }

    const uintptr_t at = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    const bool inGame = at >= 0x400000 && at < 0x1700000;
    if (_InterlockedIncrement(&(inGame ? g_vehGameLines : g_vehLines)) > 40) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const CONTEXT* c = ep->ContextRecord;
    uint32_t st[12] = {};

    __try {
        for (int i = 0; i < 12; ++i) {
            st[i] = reinterpret_cast<const uint32_t*>(c->Esp)[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    Rs7Log("veh: thread %lu code %08lX at %08lX (info %08lX %08lX) eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX "
           "edi %08lX ebp %08lX esp %08lX | %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
           GetCurrentThreadId(), code, reinterpret_cast<unsigned long>(ep->ExceptionRecord->ExceptionAddress),
           ep->ExceptionRecord->NumberParameters > 0
               ? static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[0])
               : 0ul,
           ep->ExceptionRecord->NumberParameters > 1
               ? static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[1])
               : 0ul,
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, st[0], st[1], st[2], st[3], st[4], st[5],
           st[6], st[7], st[8], st[9], st[10], st[11]);

    if (code == 0xC0000005) {
        // Stack walk by scan: every dword above ESP that points just behind a call instruction (E8 rel32,
        // FF /2 with a 2, 3 or 6 byte operand) in an executable module: the return addresses, as module+offset.
        char buf[1400];
        int n = 0;
        const uintptr_t self = reinterpret_cast<uintptr_t>(&__ImageBase);

        __try {
            for (int i = 0; i < 4096 && n < 1300; ++i) {
                const uint32_t v = reinterpret_cast<const uint32_t*>(c->Esp)[i];
                HMODULE m = nullptr;
                if (v < 0x10000 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                       reinterpret_cast<LPCWSTR>(uintptr_t(v)), &m)) {
                    continue;
                }

                const uint8_t* p = reinterpret_cast<const uint8_t*>(uintptr_t(v));
                bool call = false;

                __try {
                    call = p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
                           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }

                if (!call) {
                    continue;
                }

                const uintptr_t base = reinterpret_cast<uintptr_t>(m);
                const char* tag = base == 0x400000 ? "game" : base == self ? "rs7" : "mod";

                n += std::snprintf(buf + n, sizeof buf - n, " [+%X]%s:%08X", i * 4, tag,
                                   unsigned(base == 0x400000 ? v : v - base));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        buf[n < int(sizeof buf) ? n : int(sizeof buf) - 1] = 0;
        Rs7Log("veh: return addresses (game = absolute, rs7/mod = offset in the module):%s", buf);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void Rs7_Startup() {
    Rs7Log("rs7string %s loaded", RS7_VERSION);
    // Before any site is patched: the stubs read g_rs7_diag and g_rs7_fx_rgb (g_rs7_v6 stays 0 = the stock preview row
    // until Step 2).
    const Rs7Config cfg = LoadConfig();

    if (GetPrivateProfileIntW(L"debug", L"veh_log", 0, IniPath().c_str())) {
        AddVectoredExceptionHandler(1, VehLog);
        Rs7Log("debug: veh_log=1 -> the first 40 exceptions (plus 40 in the game image) are logged (diagnostic)");
    }

    if (GetPrivateProfileIntW(L"debug", L"spin_probe", 0, IniPath().c_str())) {
        Rs7SpinProbe_Start();
    }
    g_rs7_publish_trace = GetPrivateProfileIntW(L"debug", L"publish_trace", 0, IniPath().c_str()) != 0;
    // The overlay's view of every key (it edits them later; nothing is applied here).
    Rs7Set_InitFromIni(IniPath().c_str());
    Rs7Overlay_Configure(IniPath().c_str());

    // The exe is VMProtect-packed; d3d9.dll loading is the signal the CDLC enabler also uses. Polled every
    // 50 ms: the game reads PSARC TOCs from then on and the plain-PSARC sites must be in place first.
    for (int i = 0; i < 1200 && !GetModuleHandleW(L"d3d9.dll"); ++i) {
        Sleep(50);
    }

    const DWORD d3d9Tick = GetTickCount();  // Step 2 hooks the device no earlier than 15 s after this

    const HMODULE exe = GetModuleHandleW(nullptr);
    const DWORD checksum = Nt(exe)->OptionalHeader.CheckSum;

    if (checksum != kRs7PeChecksum) {
        Rs7Log("unsupported game build: PE checksum %08X, need %08X - nothing patched", checksum, kRs7PeChecksum);
        Fail("unsupported game build (PE checksum)");
        return;
    }

    uintptr_t textVa = 0;
    size_t textSize = 0;

    if (!TextSection(exe, &textVa, &textSize)) {
        Rs7Log("no .text section - nothing patched");
        Fail("no .text section");
        return;
    }

    // Plain PSARC / SNG first, on its own (core/plainpsarc.h): a failure there disables only it.
    // [debug] plain_delay_ms (test only): start it later, so a CDLC enabler with its own plain-PSARC
    // patches gets there first (the coexistence path).
    if (const UINT delay = GetPrivateProfileIntW(L"debug", L"plain_delay_ms", 0, IniPath().c_str())) {
        Rs7Log("debug: plain_delay_ms=%u -> plain PSARC starts late (test only)", delay < 30000 ? delay : 30000u);
        Sleep(delay < 30000 ? delay : 30000u);
    }

    Rs7Plain_Start(GetPrivateProfileIntW(L"psarc", L"plain", 1, IniPath().c_str()) != 0, kRs7PsarcSites,
                   kRs7PsarcSiteCount, textVa, textSize);

    Rs7ResolveResult res;
    const DWORD resolveTick = GetTickCount();
    int attempts = 0;

    for (int attempt = 0; attempt < 20; ++attempt) {  // .text may still be unpacking
        ++attempts;
        res = Rs7_ResolveAll(kRs7Sites, kRs7SiteCount, reinterpret_cast<const uint8_t*>(textVa), textSize, textVa);
        if (res.ok) {
            break;
        }

        Sleep(500);
    }

    Rs7Log("resolve: %d sites in %lu ms (%d attempt%s)", kRs7SiteCount, GetTickCount() - resolveTick, attempts,
           attempts == 1 ? "" : "s");

    if (!res.ok) {
        // Unresolved sites disable their own feature group; only an unresolved core site is fatal.
        const std::string first = res.errors.empty() ? std::string("sites not resolved") : res.errors.front();

        if (!Rs7_IsolateResolveFailures(res, [](const std::string& m) { Rs7Log("%s", m.c_str()); })) {
            Rs7Log("nothing patched");
            Fail(first.c_str());
            return;
        }
    }

    // Construct the side store's map here, on the worker thread: its first use must not be inside
    // a game frame (S4/S1), where the allocation could throw.
    Rs7Store_ClearAll();

    // Bind every stub's variables before any jump to it exists.
    std::vector<const Rs7Hook*> hooks;

    for (const auto& r : res.sites) {
        const Rs7Hook* h = Rs7_FindHook(r.site->id);

        if (!h) {
            Rs7Log("no stub for site %s - nothing patched", r.site->id);
            Fail("a site has no stub");
            return;
        }

        if (h->resume) {
            *h->resume = r.addr + static_cast<uintptr_t>(r.site->length);
        }
        if (h->target) {
            *h->target = r.target;
        }
        hooks.push_back(h);
    }

    // [neck] native_colours=1 widens the ColourManager at creation (CM* sites: 0xDD0 -> 0x1150 bytes,
    // 6 -> 8 columns). The game builds its one manager once, ~12 s after launch (fix/colours2, measured
    // with tests/colours2/cmtime.py); one that already exists keeps the stock 6-column layout in a
    // 0xDD0 block, which the 8-column readers would misread (rotated string colours, black 7th string)
    // and a rebuild would overflow. Then the native matrix is refused.
    if (g_rs7_native_colours) {
        void* const mgr = rs7colour::ExistingManager(g_rs7_colormgr);

        if (mgr) {
            Rs7Log("colour: the game built its ColourManager (%p) before the sites were written (%.1f s after "
                   "d3d9) - native_colours OFF (stock six-column matrix)",
                   mgr, (GetTickCount() - d3d9Tick) / 1000.0);
            g_rs7_native_colours = 0;
        } else {
            Rs7Log("colour: ColourManager not built yet (%.1f s after d3d9) - native 8-column layout from its creation",
                   (GetTickCount() - d3d9Tick) / 1000.0);
        }
    }

    // The native strings: the function copies are built from the live (still unpatched) code; if
    // any differs from the generated one, native strings are off (and with them the 7-string unit: nothing patched).
    if (g_rs7_native_strings && !Rs7Gs_BuildCopies()) {
        g_rs7_native_strings = 0;
    }
    g_rs7_native_chordloops = cfg.nativeChordloops && g_rs7_native_strings ? 1 : 0;
    Rs7Log("config: [chords] native_chordloops=%d -> %s", cfg.nativeChordloopsRaw,
           g_rs7_native_chordloops
               ? "native 8-slot chord bends / sustain techniques (CL* sites), 7/8-string chord frames (B1/B2)"
           : cfg.nativeChordloops ? "off: native_strings is unavailable (CL* sites unwritten)"
                                  : "off (CL* sites unwritten)");

    // Evaluate after native-string initialization: a failed copy is a missing prerequisite.
    // Native 7-string mode = C-lite (the guitar is built with the arrangement's count) + the publication ports (that
    // can show it); neither works alone. The native tuner (core/tuner8.h) needs the native GuitarStrings, FretGrid and
    // colour matrix
    // (its copies carry their in-place edits). Copies are built from the live code.
    if (g_rs7_native_tuner && (!g_rs7_native_strings || !g_rs7_native_fretgrid || !g_rs7_native_colours)) {
        Rs7Log("tuner8: native_tuner OFF - needs the native strings, FretGrid and colours (now %d %d %d)",
               int(g_rs7_native_strings), int(g_rs7_native_fretgrid), int(g_rs7_native_colours));
        g_rs7_native_tuner = 0;
    }

    if (g_rs7_native_tuner && !Rs7Tuner8_BuildCopies()) {
        g_rs7_native_tuner = 0;
    }

    // ... and the native tuner (fix/clite3, docs/ARCHITECTURE.md): the stock tuner session keeps six slots per
    // string array (peaks +80h, letters, picks) and its loops run to info+0Bh, so a guitar built with 7 must only ever
    // meet a native 8-slot session.
    const bool cliteMode = g_rs7_native_pegset && g_rs7_native_tuner;

    Rs7Publish_Configure(cliteMode);
    if (g_rs7_publish_ports && (!Rs7Publish_BuildCopies() || !Rs7PublishPitch_BuildCopies())) {
        g_rs7_publish_ports = g_rs7_native_publish = 0;
    }
    if (g_rs7_native_publish) {
        Rs7Log("native_publish: ON - native 8-slot layouts and ports; a guitar shows the string count it was built "
               "with (C-lite)");
    }

    // C-lite (core/clite.h): a guitar BUILT with 7/8 strings needs the native ports (with all their prerequisites,
    // which include every native layout) and the native PegSet.
    g_rs7_clite = cliteMode && g_rs7_native_publish ? 1 : 0;
    Rs7Log("clite: %s", g_rs7_clite ? "guitar rebuilt with the arrangement's string count at selection (GK* sites; "
                                      "PSQ1 replaced by GKC)"
                                    : "off: a native layout or the native ports are unavailable");

    // Diagnostics off: D2/D3/D5 stay resolved above (all or nothing still covers them) but unwritten.
    // [debug] skip_sites=ID,ID,... (test bisection): resolved, not written. Stubs that jump to a
    // skipped site's continuation are unaffected; skipping one half of a stub pair is the tester's call.
    wchar_t skipW[512] = {};

    GetPrivateProfileStringW(L"debug", L"skip_sites", L"", skipW, 512, IniPath().c_str());
    std::string skip = ",";

    for (const wchar_t* c = skipW; *c; ++c) {
        if (*c != L' ') {
            skip += char(*c & 0x7F);
        }
    }

    skip += ",";

    // The grown session (allocation, init, sized free), its copies and mid-hooks are one contract: a
    // debug skip of any TU8* / TS8* site turns the whole switch off before anything is patched.
    if (g_rs7_native_tuner && (skip.find(",TU8") != std::string::npos || skip.find(",TS8") != std::string::npos)) {
        g_rs7_native_tuner = 0;
        Rs7Log("tuner8: a native tuner site was skipped - native_tuner off");
    }

    if (g_rs7_native_tuner) {
        Rs7Log("tuner8: native_tuner ON - native 8-slot tuner sessions");
    }

    if (!g_rs7_native_tuner && g_rs7_clite) {  // a skipped tuner site: clite (and its native_publish half) go too
        Rs7_DisableFeature(Rs7Feature::Clite);
        Rs7Log("clite: off - the native tuner was turned off (a 7-built guitar needs the 8-slot tuner session)");
    }

    // Since 1.0 the native layouts have no legacy fallback (hooks/registry.h Rs7_IsSevenUnit): one member that is off
    // after the checks above leaves the game stock - nothing of the main batch is written (plain PSARC is separate).
    if (const char* missing = MissingSevenUnitMember()) {
        Rs7Log("7-string support OFF: %s is unavailable (no legacy fallback since 1.0) - nothing patched, the game "
               "stays stock (six strings)",
               missing);
        Fail("native 7-string unit incomplete (see the log)");
        return;
    }

    // Prepare every enabled generic hook before writing any site. The same Jmp
    // sites.json entries retain sitecheck's whole-instruction/no-inbound checks.
    // Process lifetime ownership: rollback restores sites, but never frees code
    // that a thread could still be executing. Startup runs once.
    // A failing site disables only its own feature group (hooks/isolate.h); the round then restarts
    // with that group gone. Only a core failure aborts everything.
    static Rs7Mid::Hook midHooks[kRs7SiteCount];
    Rs7PatchOps ops;

    ops.count = res.sites.size();
    ops.feature = [&](size_t i) {
        return Rs7_FeatureOf(*hooks[i]);
    };
    ops.name = [&](size_t i) {
        return res.sites[i].site->id;
    };
    ops.log = [](const std::string& m) {
        Rs7Log("%s", m.c_str());
    };
    // [neck] native_fretgrid: the FGR layout sites (not the FGHK / FGCT mid-hooks) are written later on the game
    // thread (rs7fg::OnGuitarUpdate); they are not part of this batch.
    auto deferredFg = [&](size_t i) {
        return hooks[i]->fg == kRs7FgNative && std::strcmp(res.sites[i].site->id, "FGHK") != 0 &&
               std::strcmp(res.sites[i].site->id, "FGCT") != 0;
    };

    ops.should = [&](size_t i) {
        if (!Rs7_ShouldPatch(*hooks[i], g_rs7_diag != 0, Rs7Vg_Enabled(), g_rs7_native_pegset != 0,
                             g_rs7_native_strings != 0)) {
            return false;
        }

        if (skip.size() > 2 && skip.find("," + std::string(res.sites[i].site->id) + ",") != std::string::npos) {
            Rs7Log("site %s skipped ([debug] skip_sites)", res.sites[i].site->id);
            return false;
        }

        if (deferredFg(i)) {
            return false;
        }

        return true;
    };
    std::vector<std::vector<uint8_t>> writeBytes(res.sites.size());

    ops.prepare = [&](size_t i) {
        if (res.sites[i].site->kind == Rs7Kind::Bytes) {  // parsed here: apply runs inside the freeze
            if (writeBytes[i].empty()) {
                writeBytes[i] = WriteBytesFor(res.sites[i]);
            }
            if (writeBytes[i].empty()) {
                return false;
            }
        }

        if (!hooks[i]->callback || midHooks[i].Entry()) {
            return true;
        }

        const auto& r = res.sites[i];

        return r.site->kind == Rs7Kind::Jmp &&
               midHooks[i].Prepare(r.addr, r.original.data(), r.original.size(), hooks[i]->callback);
    };
    ops.apply = [&](size_t i) {
        return Apply(res.sites[i], *hooks[i], midHooks[i].Entry(), writeBytes[i]);
    };
    ops.range = [&](size_t i) {
        return RangeOf(res.sites[i]);
    };
    // review #9: every round under a thread freeze; review #8: read-back verified
    ops.restore = [&](size_t i) {
        const Rs7Resolved& r = res.sites[i];

        return Rs7_RestoreBytes(r.addr, r.original.data(), r.original.size());
    };
    std::vector<size_t> todo;
    const Rs7PatchResult pr = Rs7_PatchIsolated(ops, todo);

    if (pr == Rs7PatchResult::FreezeTimeout) {
        Fail("patching failed (a game thread stayed inside a site; nothing written)");
        return;
    }

    if (pr == Rs7PatchResult::Unrecoverable) {
        // Review #8: some sites could not be restored and stay ours (their groups are disabled); no retries.
        Rs7Log("UNRECOVERABLE: %u site(s) left written after a failed rollback - patching stopped",
               unsigned(todo.size()));
        Fail("patching failed (UNRECOVERABLE rollback; see the log)");
        return;
    }

    if (pr != Rs7PatchResult::Ok) {
        Fail("patching failed (core site; restored)");
        return;
    }

    if (g_rs7_clite) {
        Rs7Clite_Start(cfg.eight);
    }
    // The deferred FretGrid sites: the final round's feature state decides (a disabled Fretgrid group has none).
    g_deferredFg.clear();

    for (size_t i = 0; i < res.sites.size(); ++i) {
        if (deferredFg(i) && !hooks[i]->ref &&
            Rs7_ShouldPatch(*hooks[i], g_rs7_diag != 0, Rs7Vg_Enabled(), g_rs7_native_pegset != 0,
                            g_rs7_native_strings != 0) &&
            !(skip.size() > 2 && skip.find("," + std::string(res.sites[i].site->id) + ",") != std::string::npos)) {
            g_deferredFg.push_back({res.sites[i], hooks[i], WriteBytesFor(res.sites[i])});
        }
    }

    g_deferredRanges.clear();

    for (const DeferredSite& d : g_deferredFg) {
        g_deferredRanges.push_back(RangeOf(d.r));
    }

    rs7fg::SetDeferredApply(g_deferredFg.empty() ? nullptr : &ApplyDeferredFg);

    for (size_t i = 0; i < res.sites.size(); ++i) {
        const Rs7Resolved& r = res.sites[i];

        if (hooks[i]->ref) {
            Rs7Log("resolved %-7s at %08X -> %08X", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else if (deferredFg(i) && Rs7_ShouldPatch(*hooks[i], g_rs7_diag != 0, Rs7Vg_Enabled(),
                                                    g_rs7_native_pegset != 0, g_rs7_native_strings != 0)) {
            Rs7Log("deferred %-4s at %08X (written on the game thread by the first guitar update)", r.site->id,
                   unsigned(r.addr));
        } else if (Rs7_ShouldPatch(*hooks[i], g_rs7_diag != 0, Rs7Vg_Enabled(), g_rs7_native_pegset != 0,
                                   g_rs7_native_strings != 0)) {
            Rs7Log("patched %-4s at %08X (original target %08X)", r.site->id, unsigned(r.addr), unsigned(r.target));
        } else {
            Rs7Log("resolved %-4s at %08X (%s off: not patched)", r.site->id, unsigned(r.addr),
                   hooks[i]->vgOnly                     ? "[test] vguitar"
                   : hooks[i]->pegs == kRs7PegNative    ? "the native PegSet"
                   : hooks[i]->strs == kRs7StrNative    ? "the native GuitarStrings"
                   : hooks[i]->fg == kRs7FgNative       ? "the native FretGrid"
                   : hooks[i]->pitch == kRs7PitchNative ? "the native pitch storage"
                   : hooks[i]->ng == kRs7NgNative       ? "the native next-note grid"
                                                        : "diagnostics");
        }
    }

    Rs7Log("%u of %u sites patched, %u FretGrid sites deferred to the first guitar update (memory protection via %s)",
           unsigned(todo.size()), unsigned(res.sites.size()), unsigned(g_deferredFg.size()), Rs7_ProtectMethod());
    Rs7Plain_LogState("after the main batch");

    {
        Rs7StartStatus st;

        st.state = Rs7StartState::Patched;
        st.written = unsigned(todo.size());
        st.resolved = unsigned(res.sites.size());
        std::snprintf(st.protect, sizeof st.protect, "%s", Rs7_ProtectMethod());
        Rs7Status_SetStart(st);
    }

    // Step 2: the ramp copy (D3D hooks). Everything above is Step 1 and stays in force whatever happens here.
    // The same worker finds the device for the overlay ([overlay] enabled=1), with or without the ramp copy.
    const bool ramp = cfg.rampSwap;
    const bool overlay = Rs7Overlay_Enabled();

    if (!ramp) {
        Rs7Log("ramp: off (ramp_swap=0): string 6 uses the preview row");
    }
    if (!ramp && !overlay) {
        return;
    }

    const Rs7SwapCfg sc{{cfg.string6Rgb, cfg.string6RgbCb}, cfg.allowInexact};

    Rs7Swap_Configure(sc);
    Rs7WorkerOpts opts;

    opts.ramp = ramp;

    if (overlay) {
        opts.onDevice = Rs7Overlay_OnDevice;
        opts.recheck = Rs7Present_Recheck;
    }

    Rs7Ramp_RunWorker(sc, d3d9Tick, opts);  // does not return
}
