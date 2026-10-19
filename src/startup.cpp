#include "startup.h"
#include "d3d/present.h"
#include "d3d/worker.h"
#include "features/allocstats/allocstats.h"
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/diagrams/diagrams.h"
#include "features/fretgrid/fretgrid.h"
#include "features/frets/frets.h"
#include "features/headstock/headstock.h"
#include "features/nextgrid/nextgrid.h"
#include "features/pitch/pitch.h"
#include "features/psarc/psarc.h"
#include "features/publish/publish.h"
#include "features/scene/scene.h"
#include "features/songend/songend.h"
#include "features/string7/string7.h"
#include "features/strings/strings.h"
#include "features/testdrive/testdrive.h"
#include "features/tuner/tuner.h"
#include "features/vguit/vguit.h"
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "model/eight.h"
#include "model/status.h"
#include "overlay/overlay.h"
#include "platform/isolate.h"
#include "platform/log.h"
#include "platform/protect.h"
#include "platform/resolve.h"
#include "platform/retry.h"
#include "platform/settings.h"
#include "platform/spinprobe.h"
#include "settings_removed.h"
#include "startup_sites.h"
#include "version.h"
#include <windows.h>
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

Rs7ColourConfig LoadConfig() {
    const std::wstring ini = IniPath();
    const bool found = Rs7Set_ReadIni(ini.c_str());  // every feature's startup read
    const Rs7DiagConfig& d = Rs7Diag_Config();
    const Rs7ColourConfig c = Rs7Colour_Config();
    const Rs7String7Config& s7 = Rs7String7_Config();
    const Rs7FretConfig& f = Rs7Frets_Config();

    g_rs7_eight = Rs7Eight_Config().eight ? 1 : 0;  // before any module reads the preparation gate
    g_rs7_diag = d.diagnostics ? 1 : 0;
    const uint32_t rgb[2] = {c.string6Rgb, c.string6RgbCb};

    rs7colour::SetPalette(rgb);  // string 6's colour
    Rs7Log("config %ls: %s", ini.c_str(), found ? "found" : "not found (defaults)");
    Rs7Log("config: [debug] diagnostics=%d -> diagnostics %s", d.diagnosticsRaw,
           d.diagnostics ? "ON (D1-D5 log lines; D2/D3/D5 patched)" : "off (D2/D3/D5 resolved, not patched)");
    Rs7Log("config: [color] string6_color=%06X%s string6_color_cb=%06X%s ramp_swap=%d allow_inexact=%d",
           unsigned(c.string6Rgb),
           c.colorValid ? (c.string6Raw.empty() ? " (default)" : "") : " (INVALID text, default used)",
           unsigned(c.string6RgbCb),
           !c.colorCbSet    ? " (= string6_color)"
           : c.colorCbValid ? ""
                            : " (INVALID text, string6_color used)",
           c.rampSwapRaw, c.allowInexactRaw);
    g_rs7_s7_on = s7.string7Line ? 1 : 0;
    Rs7Log("config: [neck] string7_line=%d -> 7th string line %s", s7.string7LineRaw,
           s7.string7Line ? "ON" : "off (N1 / N21 request stock counts, N12 keeps string 6 out of the SNG mask)");
    g_rs7_s7_v = s7.string7V;
    Rs7Log("config: [neck] string7_v=%.2f%s", double(s7.string7V),
           s7.string7VValid ? (s7.string7VRaw.empty() ? " (default)" : "") : " (INVALID text, default used)");
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
    rs7ng::Configure(true);               // mandatory since 1.0
    Rs7Headstock_Configure(ini.c_str());  // [neck] headstock_peg, [assets] (features/headstock/headstock.h)
    Rs7Alloc_Configure(ini.c_str());
    Rs7Chord_Configure(ini.c_str());
    Rs7Diagram_Configure(ini.c_str());

    Rs7Vg_StartFromIni(ini.c_str());
    Rs7Variant_ReadIni(ini.c_str());  // [test] force_* (features/testdrive/testdrive.h)

    g_rs7_frets_on = f.fretExtend ? 1 : 0;
    g_rs7_fret_halflen = f.fretHalfLen;
    Rs7Log("config: [neck] fret_extend=%d fret_halflen=%.3f%s -> fret bars %s", f.fretExtendRaw, double(f.fretHalfLen),
           f.fretHalfLenValid ? (f.fretHalfLenRaw.empty() ? " (default)" : "") : " (INVALID text, default used)",
           f.fretExtend ? "span the 7th string on 7-string songs (F1/F2)" : "stock (F1 replays stock)");
    if (!f.fretHalfLenValid) {
        Rs7Log("config: [neck] fret_halflen text was \"%ls\"", f.fretHalfLenRaw.c_str());
    }
    if (Rs7Eight_Config().eight) {
        Rs7Log("config: [neck] eight=1 -> 8-string preparation (T8 columns 6/7, vguitar string 7); native "
               "publish/tuner acceptance still required");
    }
    Rs7Tuner_Configure();  // [tuner], [debug] tuner_autotune (features/tuner/tuner.h)

    g_rs7_endlog = Rs7SongEnd_Config() ? 1 : 0;
    Rs7Log("config: [debug] song_end_log=%d -> %s", int(Rs7SongEnd_Config()),
           Rs7SongEnd_Config() ? "song state changes and the route that ends a song are logged (SE1-SE3, issue #13)"
                               : "off");
    if (!s7.string7VValid) {
        Rs7Log("config: [neck] string7_v text was \"%ls\"", s7.string7VRaw.c_str());
    }
    if (!c.colorValid) {
        Rs7Log("config: [color] string6_color text was \"%ls\"", c.string6Raw.c_str());
    }
    if (!c.colorCbValid) {
        Rs7Log("config: [color] string6_color_cb text was \"%ls\"", c.string6CbRaw.c_str());
    }

    return c;
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

// The game's code section (filled by FindGameText).
struct GameText {
    uintptr_t va = 0;
    size_t size = 0;
};

// [debug] veh_log, [debug] spin_probe.
void StartDebugProbes() {
    if (GetPrivateProfileIntW(L"debug", L"veh_log", 0, IniPath().c_str())) {
        AddVectoredExceptionHandler(1, VehLog);
        Rs7Log("debug: veh_log=1 -> the first 40 exceptions (plus 40 in the game image) are logged (diagnostic)");
    }

    if (GetPrivateProfileIntW(L"debug", L"spin_probe", 0, IniPath().c_str())) {
        Rs7SpinProbe_Start();
    }
}

// Every setting, before any site is patched: the stubs read g_rs7_diag and g_rs7_fx_rgb (g_rs7_v6 stays 0 = the stock
// preview row until Step 2).
Rs7ColourConfig LoadStartupConfig() {
    Rs7ColourConfig cfg = LoadConfig();

    StartDebugProbes();
    Rs7Publish_ReadIni(IniPath().c_str());
    // The overlay's view of every key (it edits them later; nothing is applied here).
    Rs7Set_InitFromIni(IniPath().c_str());
    Rs7Overlay_Configure(IniPath().c_str());

    return cfg;
}

// The exe is VMProtect-packed; d3d9.dll loading is the signal the CDLC enabler also uses. Polled every 50 ms: the game
// reads PSARC TOCs from then on and the plain-PSARC sites must be in place first. Returns the tick after which Step 2
// may hook the device (no earlier than 15 s later).
DWORD WaitForGame(Rs7ColourConfig& cfg) {
    Rs7_Retry(1200, 50, [] { return GetModuleHandleW(L"d3d9.dll") != nullptr; });
    Rs7Colour_Start(cfg);  // RSMods' colour and glow; cfg's string 6 colours become the effective ones

    return GetTickCount();
}

// The supported build's .text section; false (and Fail) for anything else.
bool FindGameText(GameText& text) {
    const HMODULE exe = GetModuleHandleW(nullptr);
    const DWORD checksum = Nt(exe)->OptionalHeader.CheckSum;

    if (checksum != kRs7PeChecksum) {
        Rs7Log("unsupported game build: PE checksum %08X, need %08X - nothing patched", checksum, kRs7PeChecksum);
        Fail("unsupported game build (PE checksum)");
        return false;
    }

    if (!TextSection(exe, &text.va, &text.size)) {
        Rs7Log("no .text section - nothing patched");
        Fail("no .text section");
        return false;
    }

    return true;
}

// Plain PSARC / SNG first, on its own (features/psarc/psarc.h): a failure there disables only it. Likewise
// [test] fast and the UI state log.
void StartIsolatedFeatures(const GameText& text) {
    Rs7Plain_StartFromIni(kRs7PlainHost, IniPath().c_str(), kRs7PsarcSites, kRs7PsarcSiteCount, text.va, text.size);
    Rs7Fast_Start(IniPath().c_str(), kRs7FastSites, kRs7FastSiteCount, text.va, text.size);  // [test] fast
    Rs7Ui_Start();  // log only: "ui: OLD -> NEW" for test drivers
}

// Resolves the main batch's sites. Unresolved sites disable their own feature group; only an unresolved core site is
// fatal (false, after Fail).
bool ResolveSites(const GameText& text, Rs7ResolveResult& res) {
    const DWORD resolveTick = GetTickCount();
    const int attempts = Rs7_Retry(20, 500, [&] {  // .text may still be unpacking
        res = Rs7_ResolveAll(kRs7Sites, kRs7SiteCount, reinterpret_cast<const uint8_t*>(text.va), text.size, text.va);

        return res.ok;
    });

    Rs7Log("resolve: %d sites in %lu ms (%d attempt%s)", kRs7SiteCount, GetTickCount() - resolveTick, attempts,
           attempts == 1 ? "" : "s");

    if (res.ok) {
        return true;
    }

    const std::string first = res.errors.empty() ? std::string("sites not resolved") : res.errors.front();

    if (!Rs7_IsolateResolveFailures(res, kRs7Groups, [](const std::string& m) { Rs7Log("%s", m.c_str()); })) {
        Rs7Log("nothing patched");
        Fail(first.c_str());
        return false;
    }

    return true;
}

// Binds every stub's variables before any jump to it exists; `hooks[i]` becomes the hook of `res.sites[i]`.
bool PrepareHooks(const Rs7ResolveResult& res, std::vector<const Rs7Hook*>& hooks) {
    // Construct the side store's map here, on the worker thread: its first use must not be inside
    // a game frame (S4/S1), where the allocation could throw.
    Rs7Store_ClearAll();

    for (const auto& r : res.sites) {
        const Rs7Hook* h = Rs7_FindHook(r.site->id);

        if (!h) {
            Rs7Log("no stub for site %s - nothing patched", r.site->id);
            Fail("a site has no stub");
            return false;
        }

        if (h->resume) {
            *h->resume = r.addr + static_cast<uintptr_t>(r.site->length);
        }
        if (h->target) {
            *h->target = r.target;
        }
        hooks.push_back(h);
    }

    return true;
}

// [debug] skip_sites=ID,ID,... (test bisection): resolved, not written, as ",ID,ID,". Stubs that jump to a skipped
// site's continuation are unaffected; skipping one half of a stub pair is the tester's call.
std::string ReadSkipSites() {
    wchar_t skipW[512] = {};

    GetPrivateProfileStringW(L"debug", L"skip_sites", L"", skipW, 512, IniPath().c_str());
    std::string skip = ",";

    for (const wchar_t* c = skipW; *c; ++c) {
        if (*c != L' ') {
            skip += char(*c & 0x7F);
        }
    }

    skip += ",";

    return skip;
}

// The native 7-string unit's startup checks, from the live (still unpatched) code. Returns [debug] skip_sites.
std::string ConfigureNativeUnit(DWORD d3d9Tick) {
    Rs7Colour_CheckManager(d3d9Tick);  // a manager the game already built keeps the stock layout
    // The native strings: the function copies are built from the live code; if any differs from the generated one,
    // native strings are off (and with them the 7-string unit: nothing patched).
    Rs7Gs_Prepare();
    Rs7Chord_StartLoops(g_rs7_native_strings != 0);
    // Evaluate after native-string initialization: a failed copy is a missing prerequisite.
    // Native 7-string mode = C-lite (the guitar is built with the arrangement's count) + the publication ports (that
    // can show it); neither works alone. The native tuner (features/tuner/tuner.h) needs the native GuitarStrings,
    // FretGrid and colour matrix (its copies carry their in-place edits). Copies are built from the live code.
    Rs7Tuner8_Prepare();
    // ... and the native tuner (fix/clite3, docs/ARCHITECTURE.md): the stock tuner session keeps six slots per
    // string array (peaks +80h, letters, picks) and its loops run to info+0Bh, so a guitar built with 7 must only ever
    // meet a native 8-slot session.
    const bool cliteMode = g_rs7_native_pegset && g_rs7_native_tuner;

    Rs7Publish_Start(cliteMode);
    // C-lite (features/clite/clite.h): a guitar BUILT with 7/8 strings needs the native ports (with all their
    // prerequisites, which include every native layout) and the native PegSet.
    Rs7Clite_Configure(cliteMode);
    // Diagnostics off: D2/D3/D5 stay resolved above (all or nothing still covers them) but unwritten.
    const std::string skip = ReadSkipSites();

    Rs7Tuner8_ApplySkip(skip);

    if (!g_rs7_native_tuner && g_rs7_clite) {  // a skipped tuner site: clite (and its native_publish half) go too
        Rs7_DisableFeature(Rs7Feature::Clite);
        Rs7Log("clite: off - the native tuner was turned off (a 7-built guitar needs the 8-slot tuner session)");
    }

    return skip;
}

// Since 1.0 the native layouts have no legacy fallback (model/groups.h Rs7_IsSevenUnit): one member that is off after
// the checks above leaves the game stock - nothing of the main batch is written (plain PSARC is separate).
bool SevenUnitComplete() {
    const char* missing = MissingSevenUnitMember();

    if (!missing) {
        return true;
    }

    Rs7Log("7-string support OFF: %s is unavailable (no legacy fallback since 1.0) - nothing patched, the game "
           "stays stock (six strings)",
           missing);
    Fail("native 7-string unit incomplete (see the log)");

    return false;
}

// The main batch (startup_sites.h); false (after Fail) unless every round succeeded.
bool ApplyPatchRounds(const Rs7ResolveResult& res, const std::vector<const Rs7Hook*>& hooks, const std::string& skip,
                      std::vector<size_t>& todo) {
    const Rs7PatchResult pr = Rs7Startup_WriteSites(res, hooks, skip, todo);

    if (pr == Rs7PatchResult::FreezeTimeout) {
        Fail("patching failed (a game thread stayed inside a site; nothing written)");
        return false;
    }

    if (pr == Rs7PatchResult::Unrecoverable) {
        // Review #8: some sites could not be restored and stay ours (their groups are disabled); no retries.
        Rs7Log("UNRECOVERABLE: %u site(s) left written after a failed rollback - patching stopped",
               unsigned(todo.size()));
        Fail("patching failed (UNRECOVERABLE rollback; see the log)");
        return false;
    }

    if (pr != Rs7PatchResult::Ok) {
        Fail("patching failed (core site; restored)");
        return false;
    }

    return true;
}

// The summary line, the plain-PSARC state and the overlay's status board.
void ReportStartup(size_t written, size_t resolved, size_t deferred) {
    Rs7Log("%u of %u sites patched, %u FretGrid sites deferred to the first guitar update (memory protection via %s)",
           unsigned(written), unsigned(resolved), unsigned(deferred), Rs7_ProtectMethod());
    Rs7Plain_LogState("after the main batch");
    Rs7StartStatus st;

    st.state = Rs7StartState::Patched;
    st.written = unsigned(written);
    st.resolved = unsigned(resolved);
    std::snprintf(st.protect, sizeof st.protect, "%s", Rs7_ProtectMethod());
    Rs7Status_SetStart(st);
}

// Step 2: the ramp copy (D3D hooks). Everything before is Step 1 and stays in force whatever happens here.
// The same worker finds the device for the overlay ([overlay] enabled=1), with or without the ramp copy.
void RunDeviceWorker(const Rs7ColourConfig& cfg, DWORD d3d9Tick) {
    const bool ramp = cfg.rampSwap;
    const bool overlay = Rs7Overlay_Enabled();

    if (!ramp) {
        Rs7Log("ramp: off (ramp_swap=0): string 6 uses the preview row");
    }
    if (!ramp && !overlay) {
        return;
    }

    const Rs7SwapCfg sc{{cfg.string6Rgb, cfg.string6RgbCb}, cfg.allowInexact, rs7rsmods::Present()};

    Rs7Swap_Configure(sc);
    Rs7WorkerOpts opts;

    opts.ramp = ramp;

    if (overlay) {
        opts.onDevice = Rs7Overlay_OnDevice;
        opts.recheck = Rs7Present_Recheck;
    }

    Rs7Ramp_RunWorker(sc, d3d9Tick, opts);  // does not return
}
}  // namespace

void Rs7_Startup() {
    Rs7Log("rs7string %s loaded", RS7_VERSION);
    Rs7ColourConfig cfg = LoadStartupConfig();
    const DWORD d3d9Tick = WaitForGame(cfg);
    GameText text;

    if (!FindGameText(text)) {
        return;
    }

    StartIsolatedFeatures(text);
    Rs7ResolveResult res;
    std::vector<const Rs7Hook*> hooks;

    if (!ResolveSites(text, res) || !PrepareHooks(res, hooks)) {
        return;
    }

    const std::string skip = ConfigureNativeUnit(d3d9Tick);
    std::vector<size_t> todo;

    if (!SevenUnitComplete() || !ApplyPatchRounds(res, hooks, skip, todo)) {
        return;
    }

    if (g_rs7_clite) {
        Rs7Clite_Start(Rs7Eight_Config().eight);
    }

    const size_t deferred = Rs7Startup_DeferFretGrid(res, hooks, skip);

    Rs7Startup_LogSites(res, hooks);
    ReportStartup(todo.size(), res.sites.size(), deferred);
    RunDeviceWorker(cfg, d3d9Tick);
}
