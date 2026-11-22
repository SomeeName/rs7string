#include "startup.h"
#include "d3d/present.h"
#include "d3d/worker.h"
#include "features/allocstats/allocstats.h"
#include "features/chords/chords.h"
#include "features/clite/clite.h"
#include "features/colours/colours.h"
#include "features/diag/diag.h"
#include "features/diagrams/diagrams.h"
#include "features/extras/extras.h"
#include "features/fretgrid/fretgrid.h"
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
#include "generated/sites.generated.h"
#include "hook_rules.h"
#include "model/status.h"
#include "overlay/overlay.h"
#include "platform/config.h"
#include "platform/isolate.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/protect.h"
#include "platform/resolve.h"
#include "platform/retry.h"
#include "platform/seh.h"
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

const std::wstring& Rsxr_ModuleDir() {
    static const std::wstring dir = [] {
        HMODULE self = nullptr;

        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Rsxr_ModuleDir), &self);
        std::wstring path(32768, L'\0');
        const DWORD n = GetModuleFileNameW(self, path.data(), DWORD(path.size()));

        if (n == 0 || n >= path.size()) {
            return std::wstring();  // failed or truncated: the ini and log fall back to relative names
        }

        path.resize(n);

        return path.substr(0, path.find_last_of(L"\\/") + 1);
    }();

    return dir;
}

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

// The settings file next to this DLL: rsxr.ini.
const std::wstring& IniPath() {
    static const std::wstring ini = RsxrConfig_FindIni(Rsxr_ModuleDir());

    return ini;
}

// The features' startup values, from the loaded settings (RsxrSet_InitFromIni), before any site is patched.
void ConfigureFeatures(const std::wstring& ini) {
    const RsxrDiagConfig& d = RsxrDiag_Config();
    const RsxrColourConfig& c = RsxrColour_Config();
    const RsxrString7Config& s7 = RsxrString7_Config();

    g_rsxr_diag = d.diagnostics ? 1 : 0;
    const uint32_t rgb[2] = {c.string6Rgb, c.string6RgbCb};

    rsxrcolour::SetPalette(rgb);  // string 6's colour
    g_rsxr_s7_on = s7.string7Line ? 1 : 0;
    g_rsxr_native_strings = 1;        // mandatory since 1.0 (the copy check below may still refuse it)
    g_rsxr_native_colours = 1;        // mandatory since 1.0 (the ColourManager check below may still refuse it)
    rsxrfg::ConfigureDeferred(true);  // mandatory since 1.0
    g_rsxr_native_pitch = 1;          // mandatory since 1.0 (needs MSVCR100's _msize, below)
    g_rsxr_native_tuner = 1;          // mandatory since 1.0 (the checks after the site resolve may still refuse it)

    // The detector / matcher come from MSVCR100's operator new: its _msize tells a grown block.
    HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");

    g_rsxr_fn_msize = crt ? reinterpret_cast<uintptr_t>(GetProcAddress(crt, "_msize")) : 0;

    if (!g_rsxr_fn_msize) {
        g_rsxr_native_pitch = 0;
        RsxrLog("pitch8: MSVCR100 _msize not found - the native 8-entry tuning storage is unavailable");
    }

    rsxrng::Configure(true);               // mandatory since 1.0
    RsxrHeadstock_Configure(ini.c_str());  // [assets] (features/headstock/headstock.h)
    RsxrAlloc_Configure(ini.c_str());
    RsxrChord_Configure();
    RsxrDiagram_Configure(ini.c_str());

    RsxrPrivate_Start(ini.c_str());    // the private extras (src/features/extras/extras.h)
    RsxrVariant_ReadIni(ini.c_str());  // [test] force_* (features/testdrive/testdrive.h)

    RsxrTuner_Configure();  // [tuner], [debug] tuner_autotune (features/tuner/tuner.h)

    g_rsxr_endlog = RsxrSongEnd_Config() ? 1 : 0;
}

// The startup config's log lines (the loaded settings).
void LogConfig(const std::wstring& ini, bool found) {
    const RsxrDiagConfig& d = RsxrDiag_Config();
    const RsxrColourConfig& c = RsxrColour_Config();
    const RsxrString7Config& s7 = RsxrString7_Config();

    RsxrLog("config %ls: %s", ini.c_str(), found ? "found" : "not found (defaults)");

    RsxrLog("config: [debug] diagnostics=%d -> diagnostics %s", d.diagnosticsRaw,
            d.diagnostics ? "ON (D1-D5 log lines; D2/D3/D5 patched)" : "off (D2/D3/D5 resolved, not patched)");
    RsxrLog("config: [color] string6_color=%06X%s string6_color_cb=%06X%s", unsigned(c.string6Rgb),
            c.colorValid ? (c.string6Raw.empty() ? " (default)" : "") : " (INVALID text, default used)",
            unsigned(c.string6RgbCb),
            !c.colorCbSet    ? " (= string6_color)"
            : c.colorCbValid ? ""
                             : " (INVALID text, string6_color used)");
    RsxrLog("config: [neck] string7_line=%d -> 7th string line %s", s7.string7LineRaw,
            s7.string7Line ? "ON" : "off (N1 / N21 request stock counts, N12 keeps string 6 out of the SNG mask)");
    RsxrLog("config: [debug] song_end_log=%d -> %s", int(RsxrSongEnd_Config()),
            RsxrSongEnd_Config() ? "song state changes and the route that ends a song are logged (SE1-SE3, issue #13)"
                                 : "off");
    if (!c.colorValid) {
        RsxrLog("config: [color] string6_color text was \"%ls\"", c.string6Raw.c_str());
    }
    if (!c.colorCbValid) {
        RsxrLog("config: [color] string6_color_cb text was \"%ls\"", c.string6CbRaw.c_str());
    }
    if (!c.color7Valid || !c.color7CbValid) {
        RsxrLog("config: [color] string7_color / string7_color_cb INVALID text, default used");
    }
}

// The first member of the native 7-string unit whose switch is off, or nullptr when all are on.
const char* MissingSevenUnitMember() {
    if (!g_rsxr_native_colours) {
        return "the native colours (8-column ColourManager)";
    }
    if (!g_rsxr_native_pegset) {
        return "the native PegSet";
    }
    if (!g_rsxr_native_strings) {
        return "the native GuitarStrings";
    }
    if (!g_rsxr_native_fretgrid) {
        return "the native FretGrid";
    }
    if (!g_rsxr_native_pitch) {
        return "the native pitch storage";
    }
    if (!g_rsxr_native_nextgrid) {
        return "the native next-note grid";
    }
    if (!g_rsxr_native_tuner) {
        return "the native tuner session";
    }
    if (!g_rsxr_publish_ports || !g_rsxr_native_publish) {
        return "the native publication ports";
    }
    if (!g_rsxr_clite) {
        return "C-lite (the guitar build with 7/8 strings)";
    }

    return nullptr;
}

void Fail(const char* why) {
    RsxrStartStatus st;

    st.state = RsxrStartState::Failed;
    std::snprintf(st.error, sizeof st.error, "%s", why);
    RsxrStatus_SetStart(st);
    g_rsxr_diagrams = 0;  // the FileOpener hook (ConfigureFeatures, before the resolve) passes every movie through
}
}  // namespace

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
// [debug] veh_log=1 (diagnostic): the first exceptions of every thread are logged by a vectored
// handler (it runs before the frame-based handlers and their chain check), then passed on.
volatile long g_vehLines = 0, g_vehGameLines = 0;  // own budget for faults in the game's image (the real crashes)
DWORD g_vehTls = TLS_OUT_OF_INDEXES;               // per thread: VehLog is running (set while it probes memory)
uintptr_t g_gameLo = 0, g_gameHi = 0;              // the game's image (SizeOfImage), set before VehLog is added

// The dwords of this thread's stack from esp up to its base (0: esp is not on it), at most `cap`. A vectored handler
// sees its own faults before its __except does, so VehLog never reads past the stack (it used to read 16 KB above
// esp, faulted at the stack top and recursed).
size_t StackDwords(uintptr_t esp, size_t cap) {
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t lo = reinterpret_cast<uintptr_t>(tib->StackLimit), hi = reinterpret_cast<uintptr_t>(tib->StackBase);

    if (esp < lo || esp >= hi) {
        return 0;
    }

    const size_t n = (hi - esp) / 4;

    return n < cap ? n : cap;
}

LONG VehLogBody(EXCEPTION_POINTERS* ep);

LONG CALLBACK VehLog(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C || code == 0x80000003 ||
        code == 0x80000004) {
        return EXCEPTION_CONTINUE_SEARCH;  // thread names, C++ throws, the packer's break / single steps
    }

    const DWORD lastError = GetLastError();

    if (g_vehTls == TLS_OUT_OF_INDEXES || TlsGetValue(g_vehTls)) {
        SetLastError(lastError);
        return EXCEPTION_CONTINUE_SEARCH;  // a fault of VehLog's own probes: its __except handles it, no recursion
    }

    TlsSetValue(g_vehTls, reinterpret_cast<void*>(1));
    const LONG r = VehLogBody(ep);

    TlsSetValue(g_vehTls, nullptr);
    SetLastError(lastError);

    return r;
}

LONG VehLogBody(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const uintptr_t at = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    const bool inGame = at >= g_gameLo && at < g_gameHi;
    if (_InterlockedIncrement(&(inGame ? g_vehGameLines : g_vehLines)) > 40) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const CONTEXT* c = ep->ContextRecord;
    uint32_t st[12] = {};
    const size_t top = StackDwords(c->Esp, 12);

    __try {
        for (size_t i = 0; i < top; ++i) {
            st[i] = reinterpret_cast<const uint32_t*>(c->Esp)[i];
        }
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
    }

    RsxrLog("veh: thread %lu code %08lX at %08lX (info %08lX %08lX) eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX "
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

        const size_t words = StackDwords(c->Esp, 4096);

        __try {
            for (size_t i = 0; i < words && n < 1300; ++i) {
                const uint32_t v = reinterpret_cast<const uint32_t*>(c->Esp)[i];
                HMODULE m = nullptr;
                if (v < 0x10000 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                       reinterpret_cast<LPCWSTR>(uintptr_t(v)), &m)) {
                    continue;
                }

                const uint8_t* p = reinterpret_cast<const uint8_t*>(uintptr_t(v));
                bool call = false;

                if (v - reinterpret_cast<uintptr_t>(m) < 0x1000) {
                    continue;  // the PE header page: no call before it (and p[-6] could leave the image)
                }

                __try {
                    call = p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
                           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
                } __except (Rsxr_FaultFilter(GetExceptionCode())) {
                }

                if (!call) {
                    continue;
                }

                const uintptr_t base = reinterpret_cast<uintptr_t>(m);
                const char* tag = base == g_gameLo ? "game" : base == self ? "rsxr" : "mod";

                const int k = std::snprintf(buf + n, sizeof buf - size_t(n), " [+%X]%s:%08X", unsigned(i * 4), tag,
                                            unsigned(base == g_gameLo ? v : v - base));

                if (k < 0 || k >= int(sizeof buf) - n) {
                    break;
                }

                n += k;
            }
        } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        }

        buf[n < int(sizeof buf) ? n : int(sizeof buf) - 1] = 0;
        RsxrLog("veh: return addresses (game = absolute, rsxr/mod = offset in the module):%s", buf);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// The game's code section (filled by FindGameText).
struct GameText {
    uintptr_t va = 0;
    size_t size = 0;
};

// A fault inside a hook callback (RsxrMid_Guard): the site ran without the callback. The first 20 are logged, with the
// callback as an offset into RSXR's dinput8.dll.
void __cdecl LogHookFault(uintptr_t callback, unsigned long code, long count) {
    if (count > 20) {
        return;
    }

    RsxrLog("hook fault: callback dinput8+%06lX failed with exception %08lX and was skipped; the game went on without "
            "it (fault %ld%s)",
            static_cast<unsigned long>(callback - reinterpret_cast<uintptr_t>(&__ImageBase)), code, count,
            count == 20 ? ", later faults are only counted" : "");
}

// The hook fault logger (always), [debug] veh_log, [debug] spin_probe.
void StartDebugProbes() {
    RsxrMid_SetFaultLog(&LogHookFault);

    if (GetPrivateProfileIntW(L"debug", L"veh_log", 0, IniPath().c_str())) {
        const HMODULE exe = GetModuleHandleW(nullptr);

        g_gameLo = reinterpret_cast<uintptr_t>(exe);
        g_gameHi = g_gameLo + Nt(exe)->OptionalHeader.SizeOfImage;
        g_vehTls = TlsAlloc();
        // Diagnostic only: VehLog calls RsxrLog, which allocates inside the exception handler.
        AddVectoredExceptionHandler(1, VehLog);
        RsxrLog("debug: veh_log=1 -> the first 40 exceptions (plus 40 in the game image) are logged (diagnostic)");
    }

    if (GetPrivateProfileIntW(L"debug", L"spin_probe", 0, IniPath().c_str())) {
        RsxrSpinProbe_Start();
    }
}

// Every setting, before any site is patched: the stubs read g_rsxr_diag and g_rsxr_fx_rgb (g_rsxr_v6 stays 0 = the
// stock preview row until the device worker runs).
RsxrColourConfig LoadStartupConfig() {
    const std::wstring ini = IniPath();
    // Every key, read once: the features' startup reads and the overlay's view (nothing is applied here).
    const bool found = RsxrSet_InitFromIni(ini.c_str());

    LogConfig(ini, found);
    ConfigureFeatures(ini);
    StartDebugProbes();
    RsxrPublish_ReadIni(ini.c_str());
    RsxrOverlay_Configure(ini.c_str());

    return RsxrColour_Config();
}

// The exe is VMProtect-packed; d3d9.dll loading is the signal the CDLC enabler also uses. Polled every 50 ms: the game
// reads PSARC TOCs from then on and the plain-PSARC sites must be in place first. Returns the tick the device worker
// waits from (it hooks the device no earlier than 15 s later).
DWORD WaitForGame(RsxrColourConfig& cfg) {
    Rsxr_Retry(1200, 50, [] { return GetModuleHandleW(L"d3d9.dll") != nullptr; });
    if (!GetModuleHandleW(L"d3d9.dll")) {
        RsxrLog("startup: the game did not load d3d9.dll within 60 s - patching anyway (the game may have read some "
                "song files before the PSARC sites were in place)");
    }
    RsxrColour_Start(cfg);  // RSMods' colour and glow; cfg's string 6 colours become the effective ones

    return GetTickCount();
}

// The supported build's .text section; false (and Fail) for anything else.
bool FindGameText(GameText& text) {
    const HMODULE exe = GetModuleHandleW(nullptr);
    const DWORD checksum = Nt(exe)->OptionalHeader.CheckSum;

    if (checksum != kRsxrPeChecksum) {
        RsxrLog("unsupported game build - nothing patched. RSXR supports only the Steam Remastered September 2022 "
                "build. Verify the game files in Steam or update RSXR. (PE checksum %08X, need %08X)",
                checksum, kRsxrPeChecksum);
        Fail("unsupported game build (PE checksum)");
        return false;
    }

    if (!TextSection(exe, &text.va, &text.size)) {
        RsxrLog("no .text section - nothing patched");
        Fail("no .text section");
        return false;
    }

    return true;
}

// Plain PSARC / SNG first, on its own (features/psarc/psarc.h): a failure there disables only it. Likewise
// [test] fast and the UI state log.
void StartIsolatedFeatures(const GameText& text) {
    RsxrPlain_StartFromIni(kRsxrPlainHost, IniPath().c_str(), kRsxrPsarcSites, kRsxrPsarcSiteCount, text.va, text.size);
    RsxrFast_Start(IniPath().c_str(), kRsxrFastSites, kRsxrFastSiteCount, text.va, text.size);  // [test] fast
    RsxrUi_Start();  // log only: "ui: OLD -> NEW" for test drivers
}

// Resolves the main batch's sites. Unresolved sites disable their own feature group; only an unresolved core site is
// fatal (false, after Fail).
bool ResolveSites(const GameText& text, RsxrResolveResult& res) {
    const DWORD resolveTick = GetTickCount();
    std::vector<const RsxrSite*> previous;
    const int attempts = Rsxr_Retry(20, 500, [&] {  // .text may still be unpacking
        res =
            Rsxr_ResolveAll(kRsxrSites, kRsxrSiteCount, reinterpret_cast<const uint8_t*>(text.va), text.size, text.va);

        const bool settled = Rsxr_ResolveSettled(res, previous, kRsxrSiteCount);

        previous = res.failed;

        return settled;
    });

    RsxrLog("resolve: %d sites in %lu ms (%d attempt%s)", kRsxrSiteCount, GetTickCount() - resolveTick, attempts,
            attempts == 1 ? "" : "s");

    if (res.ok) {
        return true;
    }

    const std::string first = res.errors.empty() ? std::string("sites not resolved") : res.errors.front();

    if (!Rsxr_IsolateResolveFailures(res, kRsxrGroups, [](const std::string& m) { RsxrLog("%s", m.c_str()); })) {
        RsxrLog("nothing patched");
        Fail(first.c_str());
        return false;
    }

    return true;
}

// Binds every stub's variables before any jump to it exists; `hooks[i]` becomes the hook of `res.sites[i]`.
bool PrepareHooks(const RsxrResolveResult& res, std::vector<const RsxrHook*>& hooks) {
    // Construct the side store's map here, on the worker thread: its first use must not be inside
    // a game frame (S4/S1), where the allocation could throw.
    RsxrStore_ClearAll();

    for (const auto& r : res.sites) {
        const RsxrHook* h = Rsxr_FindHook(r.site->id);

        if (!h) {
            RsxrLog("no stub for site %s - nothing patched", r.site->id);
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
    RsxrColour_CheckManager(d3d9Tick);  // a manager the game already built keeps the stock layout
    // The native strings: the function copies are built from the live code; if any differs from the generated one,
    // native strings are off (and with them the 7-string unit: nothing patched).
    RsxrGs_Prepare();
    RsxrChord_StartLoops(g_rsxr_native_strings != 0);
    // After the native strings: a failed copy is a missing prerequisite. The native tuner (features/tuner/tuner.h)
    // needs the native GuitarStrings, FretGrid and colour matrix (its copies carry their in-place edits).
    RsxrTuner8_Prepare();
    // Native 7-string mode = C-lite (the guitar is built with the arrangement's count) + the publication ports (that
    // can show it); neither works alone. Both need the native PegSet and the native tuner: the stock tuner session
    // keeps six slots per string array (peaks +80h, letters, picks) and its loops run to info+0Bh
    // (docs/ARCHITECTURE.md).
    const bool cliteMode = g_rsxr_native_pegset && g_rsxr_native_tuner;

    RsxrPublish_Start(cliteMode);
    // C-lite (features/clite/clite.h): a guitar BUILT with 7/8 strings needs the native ports (with all their
    // prerequisites, which include every native layout) and the native PegSet.
    RsxrClite_Configure(cliteMode);
    const std::string skip = ReadSkipSites();

    RsxrTuner8_ApplySkip(skip);

    if (!g_rsxr_native_tuner && g_rsxr_clite) {  // a skipped tuner site: clite (and its native_publish half) go too
        Rsxr_DisableFeature(RsxrFeature::Clite);
        RsxrLog("clite: off - the native tuner was turned off (a 7-built guitar needs the 8-slot tuner session)");
    }

    return skip;
}

// Since 1.0 the native layouts have no legacy fallback (model/groups.h Rsxr_IsSevenUnit): one member that is off after
// the checks above leaves the game stock - nothing of the main batch is written (plain PSARC is separate).
bool SevenUnitComplete() {
    const char* missing = MissingSevenUnitMember();

    if (!missing) {
        return true;
    }

    RsxrLog("7-string support OFF: %s is unavailable (no legacy fallback since 1.0) - nothing patched, the game "
            "stays stock (six strings)",
            missing);
    Fail("native 7-string unit incomplete (see the log)");

    return false;
}

// The main batch (startup_sites.h); false (after Fail) unless every round succeeded.
bool ApplyPatchRounds(const RsxrResolveResult& res, const std::vector<const RsxrHook*>& hooks, const std::string& skip,
                      std::vector<size_t>& written) {
    const RsxrPatchResult pr = RsxrStartup_WriteSites(res, hooks, skip, written);

    if (pr == RsxrPatchResult::FreezeTimeout) {
        Fail("patching failed (a game thread stayed inside a site; nothing written)");
        return false;
    }

    if (pr == RsxrPatchResult::PreconditionFailed) {
        Fail("native 7-string unit incomplete (ColourManager changed before patching; nothing written)");
        return false;
    }

    if (pr == RsxrPatchResult::Unrecoverable) {
        // Some sites could not be restored and stay ours (their groups are disabled); no retries.
        RsxrLog("patching stopped: restart the game and send rsxr.log with your bug report "
                "(UNRECOVERABLE: %u site(s) left written after a failed rollback)",
                unsigned(written.size()));
        Fail("patching failed: restart the game and send rsxr.log (UNRECOVERABLE rollback)");
        return false;
    }

    if (pr != RsxrPatchResult::Ok) {
        Fail("patching failed (core site; restored)");
        return false;
    }

    return true;
}

// The summary line, the plain-PSARC state and the overlay's status board.
void ReportStartup(size_t written, size_t resolved, size_t deferred) {
    RsxrLog("%u of %u sites patched, %u FretGrid sites deferred to the first guitar update (memory protection via %s)",
            unsigned(written), unsigned(resolved), unsigned(deferred), Rsxr_ProtectMethod());
    RsxrPlain_LogState("after the main batch");
    RsxrStartStatus st;

    st.state = RsxrStartState::Patched;
    st.written = unsigned(written);
    st.resolved = unsigned(resolved);
    std::snprintf(st.protect, sizeof st.protect, "%s", Rsxr_ProtectMethod());
    RsxrStatus_SetStart(st);
}

// The ramp copy (D3D hooks). The sites written before stay in force whatever happens here.
// The same worker finds the device for the overlay ([overlay] enabled=1). DXT ramps are never copied.
void RunDeviceWorker(const RsxrColourConfig& cfg, DWORD d3d9Tick) {
    const RsxrSwapCfg sc{{cfg.string6Rgb, cfg.string6RgbCb}, rsxrrsmods::Present()};

    RsxrSwap_Configure(sc);
    RsxrWorkerOpts opts;

    if (RsxrOverlay_Enabled()) {
        opts.onDevice = RsxrOverlay_OnDevice;
        opts.recheck = RsxrPresent_Recheck;
    }

    RsxrRamp_RunWorker(sc, d3d9Tick, opts);  // does not return
}
}  // namespace

void Rsxr_Startup() {
    RsxrLog("RS Extended Range (RSXR) %s loaded", RSXR_VERSION);
    RsxrColourConfig cfg = LoadStartupConfig();
    const DWORD d3d9Tick = WaitForGame(cfg);
    GameText text;

    if (!FindGameText(text)) {
        return;
    }

    StartIsolatedFeatures(text);
    RsxrResolveResult res;
    std::vector<const RsxrHook*> hooks;

    if (!ResolveSites(text, res) || !PrepareHooks(res, hooks)) {
        return;
    }

    const std::string skip = ConfigureNativeUnit(d3d9Tick);
    std::vector<size_t> written;

    if (!SevenUnitComplete() || !ApplyPatchRounds(res, hooks, skip, written)) {
        return;
    }

    if (g_rsxr_clite) {
        RsxrClite_Start();
    }

    const size_t deferred = RsxrStartup_DeferFretGrid(res, hooks, skip);

    RsxrStartup_LogSites(res, hooks, skip);
    ReportStartup(written.size(), res.sites.size(), deferred);
    RunDeviceWorker(cfg, d3d9Tick);
}
