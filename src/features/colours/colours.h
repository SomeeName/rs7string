#pragma once
#include "platform/config.h"  // Rs7_ParseRgb
#include "platform/midhook.h"
#include <cstdint>
#include <string>
#include <vector>

// String colours: the native eight-column ColourManager (CM* sites), string 6's own colour band in the gem ramp
// textures (Step 2, the texture swap itself is d3d/swap_*.cpp), RSMods compatibility (its Extended Range colour, its
// pure red glow, GLW).
//
// Files: rgb.cpp (colour text and channels), native.cpp (the eight-column layout and its publication into a
// manager), ramp.cpp (the ramp copy model), rampstate.cpp (the ramp candidate table and the V6 rule), rsmods.cpp
// (finding RSMods and reading RSMods.ini), glow.cpp (RSMods' glow conversions) are the logic, tests/colours;
// hooks.cpp (the CM* and GLW mid-hooks, startup), hooks_publish.cpp (which thread may write the manager) are the
// glue; state.h (the feature's state); registration.cpp (the hook tables src/registry.cpp looks the sites up in);
// settings.cpp (the [color] keys and their startup read).

struct Rs7HookTable;
struct Rs7SetTable;

// String 6's own colour (Step 1/2), read by the native colour matrix: V per colour mode, 0 = the stock preview row
// colorUV[mode][17][0] (set by the ramp swap, Step 2), and RGB [mode*3 + channel] from [color] string6_color /
// string6_color_cb, set by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) float g_rs7_v6[2];
extern "C" __declspec(dllexport) float g_rs7_fx_rgb[6];
// Data references (ref sites, never written), bound by Rs7_Startup before any site is patched.
extern "C" __declspec(dllexport) uintptr_t g_rs7_colormgr;      // &g_pRnd_ColorManager (X_CM)
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_cs;    // CRITICAL_SECTION* of the NiTexture list (X_TCS)
extern "C" __declspec(dllexport) uintptr_t g_rs7_texlist_head;  // &list head (X_THEAD)

// The CM* sites (group Colours, written only with g_rs7_native_colours) and GLW (group Glow, only with
// g_rs7_glow_fix).
extern const Rs7HookTable kRs7ColourHooks;
extern const Rs7HookTable kRs7GlowHooks;
// X_CM X_TCS X_THEAD (data references, group Core).
extern const Rs7HookTable kRs7ColourRefHooks;

// ---- settings (settings.cpp)

// String 6's default colour: light grey (owner's choice), 0xRRGGBB.
constexpr uint32_t kRs7DefaultColour = 0xC8C8C8;

// [color]: string6_color, string6_color_cb, rsmods_extended_range, ramp_swap, allow_inexact.
extern const Rs7SetTable kRs7ColourSettings;
// The Colour tab and the ramp copy status row (platform/uilayout.h).
struct Rs7UiTable;
extern const Rs7UiTable kRs7ColourUi;
// The colour manager's mode for player 0: "normal", "colourblind" or "unknown" (overlay, SEH-guarded read).
const char* Rs7Colour_ModeName();

// [color] as read at startup.
//   string6_color=RRGGBB     string 6's colour (default C8C8C8; "#" prefix allowed). Invalid text
//                            (also any value of 255 or more characters, never truncated):
//                            the default, logged.
//   string6_color_cb=RRGGBB  the same in colourblind mode (default: string6_color; invalid text:
//                            string6_color, logged).
//   ramp_swap=0|1            1 (default): copy the stock ramp textures with a string-6 band (Step 2);
//                            0: Step 1 only, no D3D hooks at all.
//   allow_inexact=0|1        1: also swap DXT1/DXT5 ramps (CPU-decoded, GPU rounding may differ).
struct Rs7ColourConfig {
    bool colorValid = true;                   // string6_color missing, empty or valid
    uint32_t string6Rgb = kRs7DefaultColour;  // 0xRRGGBB
    std::wstring string6Raw;                  // the text as read (for the log)
    bool colorCbSet = false;                  // string6_color_cb present and non-empty
    bool colorCbValid = true;                 // string6_color_cb missing, empty or valid
    uint32_t string6RgbCb = kRs7DefaultColour;
    std::wstring string6CbRaw;
    bool rsmodsExtendedRange = true;  // [color] rsmods_extended_range: use RSMods ER low B when available
    int rampSwapRaw = 1;
    bool rampSwap = true;  // raw != 0
    int allowInexactRaw = 0;
    bool allowInexact = false;  // raw != 0
};

// Reads [color] from a resolved ini path (nullptr: the defaults).
Rs7ColourConfig Rs7Colour_ReadIni(const wchar_t* iniPath);
// The last startup read (Rs7Set_ReadIni).
const Rs7ColourConfig& Rs7Colour_Config();

// ---- startup (hooks.cpp)

// After d3d9.dll appeared: finds RSMods (rs7rsmods::Init), resolves string 6's effective colours into
// cfg.string6Rgb / string6RgbCb ([color] rsmods_extended_range), stores the palette and sets g_rs7_glow_fix.
void Rs7Colour_Start(Rs7ColourConfig& cfg);
// After the sites are resolved, before any is written: the native layout is valid only for a manager the patched
// ctor builds, so a manager the game already built turns g_rs7_native_colours off. d3d9Tick: for the log.
void Rs7Colour_CheckManager(uint32_t d3d9Tick);

// ---- colour text and channels (rgb.cpp)

// out = {R, G, B} / 255.0f.
void Rs7_RgbToFloats(uint32_t rgb, float out[3]);
// One channel of the grey ramp line g recoloured to colour c around anchor a (plan D8):
// g <= a: (c*g + a/2)/a; else c + ((255-c)*(g-a) + (255-a)/2)/(255-a). Identity when c == a.
uint8_t Rs7_Recolor(uint8_t g, uint8_t anchor, uint8_t c);

// ---- the native colour matrix (native.cpp, hooks_publish.cpp)

// CONFIRMED stock layout: section 4 fretgrid-tables.md. Native compact layout:
// prefix/source lists/modes unchanged; stock six-column ABI retained through DD0h.
// Appended [2][28][8] RGB pointers at DD0h and UV floats at 14D0h; size 1BD0h.
// Stock pointer entries alias the native RGB values for RSMods. Owned RGB has process lifetime.
extern "C" __declspec(dllexport) uintptr_t g_rs7_native_colours;

namespace rs7colour {
constexpr uint32_t kPtr = 0xDD0, kUV = 0x14D0, kSize = 0x1BD0;
constexpr int kStates = 28, kModes = 2, kWidth = 8;

inline uint32_t Width() {
    return g_rs7_native_colours ? 8 : 6;
}

inline uint32_t PtrOffset(uint32_t mode, uint32_t state, uint32_t slot = 0) {
    return (g_rs7_native_colours ? kPtr : 0x350) + 4 * ((mode * 28 + state) * Width() + slot);
}

inline uint32_t UVOffset(uint32_t mode, uint32_t state, uint32_t slot = 0) {
    return (g_rs7_native_colours ? kUV : 0x890) + 4 * ((mode * 28 + state) * Width() + slot);
}

const float* GetRGB(const uint8_t* mgr, uint32_t mode, uint32_t state, uint32_t slot);
void Publish(uint8_t* mgr);  // manager build / configured palette publication, never per query
// Settings overlay (Present thread): publishes now when called on the manager's (CMBUILD) thread, else defers.
void PublishCurrent();
// Any thread (the ramp worker, StoreV6): only marks the configured palette as pending; never touches the manager.
void RequestPublish();
bool PublishPending();
// Game thread (CMBUILD, FGHK, the SetTexture hook): publishes a pending request into the finished manager.
// A no-op on any thread other than the one that ran CMBUILD.
void DrainPending();
void ResetThreadForTests();
// Patch time (fix/colours2): the native layout is only valid for a manager the patched ctor builds.
// ref = &g_pRnd_ColorManager (X_CM). Null when the game has not built its manager yet (native OK),
// else the stock 6-column manager that already exists (native_colours must stay off).
void* ExistingManager(uintptr_t ref);
// String 6's palette g_rs7_fx_rgb ([mode*3 + channel]) from rgb[mode] = 0xRRGGBB (mode 0 normal, 1 colourblind).
void SetPalette(const uint32_t rgb[2]);
}  // namespace rs7colour

void __cdecl Rs7Colour_CMINIT(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMBUILD(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMUV(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMIX(Rs7Mid::Context& c);
void __cdecl Rs7Colour_CMFALL(Rs7Mid::Context& c);

// ---- the ramp copy model (ramp.cpp)

// Ramp model for string 6's own colour band (plan D3/D4/D5/D8). Pure: no D3D, no Windows.
// The stock colour ramps (note_gradient_map.dds / gradient_map_additive.dds) are 256x128, 16 rows
// of 8 lines; the game samples row i at V = (2i+1)/32. Our copy has the stock's exact size, format
// and level count; it repaints only lines that no stock V ever reads, at V6 below.

struct Rs7Image {
    unsigned w = 0, h = 0;
    std::vector<uint32_t> px;
};  // 0xAARRGGBB, row-major

struct Rs7Tap {
    unsigned line;
    unsigned w4;
};  // weight in quarters

// Taps of V = num/den on a level of height h, D3D9 texel centres (c = V*h - 0.5), clamp addressing.
// bilinear: the 1-2 lines with nonzero weight; point: floor(V*h). Exact rational arithmetic, lines
// ascending. The line set is always exact; w4 is exact whenever the weight is a multiple of 1/4
// (every stock V on h >= 8, V6 on h >= 32), otherwise rounded into 1..3 so a nonzero tap never
// disappears.
std::vector<Rs7Tap> Rs7Ramp_Taps(unsigned num, unsigned den, unsigned h, bool bilinear);

constexpr unsigned kRs7RampRows = 16;
// V6 per mode (copy layout, D3): mode 0 = 57/128 = 0.4453125, mode 1 = 121/128 = 0.9453125.
constexpr unsigned kRs7V6Num[2] = {57, 121};
constexpr unsigned kRs7V6Den = 128;
constexpr float kRs7V6[2] = {0.4453125f, 0.9453125f};
constexpr uint8_t kRs7Anchor[2] = {0x90, 0x4C};  // D8, HYPOTHESIS (RSMods anchor table)
constexpr unsigned kRs7Row6[2] = {7, 15};        // the row the custom band is recoloured from

// Lines of a level that any stock V (2i+1)/32, i = 0..15, reads with point or bilinear filtering.
std::vector<bool> Rs7Ramp_Protected(unsigned h);

struct Rs7RampBuild {
    std::vector<Rs7Image> levels;  // same sizes as the stock levels
    unsigned customLevels = 0;     // levels 0..customLevels-1 hold the pure custom colour at V6 (both modes)
};

// Copy of `stock` with only the V6 taps (both modes, point + bilinear) repainted, on every level where
// they avoid all protected lines; content = Rs7_Recolor of the stock row kRs7Row6[mode] at that level
// (its point line; the alpha byte is copied from that line). rgb[mode] = 0xRRGGBB.
// Stops at the first level where they collide (everything below that level stays a pure copy).
Rs7RampBuild Rs7Ramp_Build(const std::vector<Rs7Image>& stock, const uint32_t rgb[2]);

struct Rs7RampCheck {
    bool exact;
    bool v6Pure;
    unsigned changedLines;
    std::string why;
};

// exact: every protected line of every level is identical to stock and nothing else outside the V6 taps
// changed; v6Pure: customLevels >= 1 and on levels < customLevels every V6 tap holds the recoloured
// line. why: the first problem found (empty if none).
Rs7RampCheck Rs7Ramp_Verify(const std::vector<Rs7Image>& stock, const Rs7RampBuild& b, const uint32_t rgb[2]);

// D3DFORMAT / D3DPOOL / D3DUSAGE values used by the policy.
constexpr uint32_t kRs7FmtA8R8G8B8 = 21, kRs7FmtX8R8G8B8 = 22;
constexpr uint32_t kRs7FmtDxt1 = 0x31545844, kRs7FmtDxt5 = 0x35545844;  // MAKEFOURCC('D','X','T','1'/'5')
constexpr uint32_t kRs7PoolManaged = 1;

enum class Rs7RampVerdict { Swap, Refuse };

struct Rs7RampDesc {
    uint32_t format;
    unsigned w, h, levels;
    uint32_t pool;
    uint32_t usage;
};

struct Rs7RampPlan {
    Rs7RampVerdict verdict;
    bool exactPossible;
    uint32_t outFormat;
    const char* why;
};

// D3/D4/D5: 256x128 level 0, >= 1 level; A8R8G8B8 (21)/X8R8G8B8 (22) -> Swap (exact, same format);
// DXT1/DXT5 -> Swap only with allowInexact (out A8R8G8B8); pool != MANAGED (1) -> Refuse; any usage
// bit (RENDERTARGET, DYNAMIC, AUTOGENMIPMAP, ...) -> Refuse; anything else -> Refuse.
Rs7RampPlan Rs7Ramp_Plan(const Rs7RampDesc& d, bool allowInexact);
uint64_t Rs7Ramp_Hash(const std::vector<Rs7Image>& levels);  // FNV-1a 64 over sizes + texels

// nonUniformRows: rows (h/16 lines each) whose lines are not all identical; grey7/grey15: every texel
// of that row has R == G == B.
struct Rs7RowStats {
    unsigned nonUniformRows;
    bool grey7, grey15;
};

Rs7RowStats Rs7Ramp_RowStats(const Rs7Image& level0);  // for the log

// ---- the ramp candidate table and the V6 rule (rampstate.cpp)

// Step 2 ramp-swap state (plan D6): the candidate table (stock ramp textures found in the engine's
// texture list), the V6 rule that drives g_rs7_v6, and counters logged at song setup.
// Pure bookkeeping: no D3D calls, candidates' texture pointers are compared, never dereferenced.

enum class Rs7Family : uint8_t { Note, Additive };
// Pending: found by the worker, never processed. Busy: the render thread is processing it (Claim).
// Active: our copy is bound in its place (stock + ours referenced). Refused: never swapped. Retry:
// processing failed transiently, again at retryAtMs. Retired: free slot (stale entry; d3d = nullptr).
enum class Rs7CandState : uint8_t { Pending, Active, Refused, Retry, Busy, Retired };

struct Rs7Cand {         // written by the worker (Pending, Retire) and the render thread (rest)
    void* volatile d3d;  // stock IDirect3DBaseTexture9* (raw until Active: compare only)
    Rs7Family family;
    volatile Rs7CandState state;
    void* stock;  // Active: IDirect3DTexture9* we hold a reference on
    void* ours;   // Active: our IDirect3DTexture9* (one reference per candidate)
    uint64_t hash;
    unsigned retries;
    uint32_t retryAtMs;
    uintptr_t obj;       // engine source texture it was found on (revalidation)
    volatile long gone;  // worker: an Active entry missing from a clean walk (render thread retires it)
    char path[120];      // platform filename, for the log
};

constexpr int kRs7MaxCands = 16;
// Writers (Add, Claim, Retire, ResetForTests) are serialised by a lock; Find is lock-free. Add reuses a
// Retired slot before appending; a slot's fields are written before its d3d pointer is published.
int Rs7Cand_Add(void* d3d, Rs7Family f, const char* path, uintptr_t obj = 0);  // -1 full; existing index if present
Rs7Cand* Rs7Cand_Find(const void* d3d);                                        // nullptr if unknown (no deref of d3d)
int Rs7Cand_Count();                                                           // slots in use (Retired included)
Rs7Cand* Rs7Cand_At(int i);                                                    // nullptr outside [0, Count)
// Render thread: Pending/Retry -> Busy iff c still holds d3d. False: leave it alone.
bool Rs7Cand_Claim(Rs7Cand* c, const void* d3d);
// -> Retired (d3d = nullptr, slot reusable) iff c still holds d3d. byOwner = false (the worker): only
// Pending/Refused/Retry. byOwner = true (the render thread, which owns Busy and Active entries; it
// releases stock/ours first): any live state. True if retired.
bool Rs7Cand_Retire(Rs7Cand* c, const void* d3d, bool byOwner);
// D6: on iff enabled && deviceOk && >= 1 Note Active && no Refused/Retry of either family.
bool Rs7Ramp_V6Rule(bool enabled, bool deviceOk);
// Applies the rule to g_rs7_v6 (both modes, atomic dword stores); logs "ramp: V6 ON/OFF (reason)" on change
// (inside the lock, so lines from concurrent callers keep the order of the stores).
void Rs7Ramp_UpdateV6(bool enabled, bool deviceOk, const char* reason);
// Counters (Interlocked) and the S4 log line.
void Rs7Ramp_CountSwap();
void Rs7Ramp_CountHidden();
void Rs7Ramp_CountFault();
// "ramp stats: swaps=%u hidden=%u faults=%u candidates %u (active %u, refused %u, retry %u, pending %u) v6=%s";
// candidates = live entries (Retired excluded), pending includes Busy.
void Rs7Ramp_LogStats();
// Tests: empties the table, zeroes the counters and g_rs7_v6 (no log line).
void Rs7Cand_ResetForTests();

// What the ramp-copy hook (d3d/swap_*.cpp, app layer) provides to the colour settings and the overlay; defined in
// d3d/swap_ramp.cpp.
// Overlay ([color] string6_color / _cb live, any thread): the render thread's next SetTexture on the
// hooked device rebuilds string 6's lines of every live copy with these colours (same build and
// self-check as the first copy) and drops cached copies no candidate uses. Later copies use them too.
void Rs7Swap_RequestRecolour(const uint32_t rgb[2]);

// Overlay status (render thread).
struct Rs7SwapStats {
    bool installed, deviceOk;
    unsigned copies, active, refused, retry, pending;
    uint32_t rgb[2];
};

void Rs7Swap_Stats(Rs7SwapStats* out);

// ---- RSMods (rsmods.cpp, glow.cpp)

namespace rs7rsmods {
struct Colour {
    bool valid = false;
    uint32_t rgb = 0;
};

Colour Read(const wchar_t* iniPath, bool loaded);
// Inspect an installed proxy's resources without executing it, then read its adjacent INI.
Colour ReadProxy(const wchar_t* proxyPath);
void Resolve(const Colour& colour, bool enabled, uint32_t (&rgb)[2]);
// Startup only. Overlay changes use the cached policy.
void Init();
const Colour& Cached();
// The proxy found by Init is RSMods (whatever its settings): RSMods may bind its own note ramps (d3d/swap.h,
// d3d/swap_foreign.cpp).
bool Present();
// True if the DLL at proxyPath identifies as RSMods (resource data only, never executed).
bool Identify(const wchar_t* proxyPath);
// RSMods.ini (read only): custom string colours (CustomStringColors=2) not shaded from the game's own colours
// (UseExactColors not on), i.e. RSMods derives each string's glow from its colour (below).
bool ReadCustomGlow(const wchar_t* iniPath);
// Init's answer for the installed proxy: Present() and ReadCustomGlow of the RSMods.ini next to it.
bool CustomGlow();

// RSMods' custom string colours derive a string's glow from its Enabled colour as HSL(hue - 4, 1, 0.5)
// (CollectColors::GetGlowStringColor). For a hue of 0..3 (pure red) the hue goes negative, its HSL2RGB
// matches no sextant and keeps a stale colour, so that string's glow outline is near black. These are
// RSMods' own conversions, float for float, plus the corrected glow with the hue wrapped to 356..359.

// RSMods' RGB2HSL hue (int, truncated, 0..359). False for a grey (RSMods leaves the hue unset).
bool Hue(const float rgb[3], int* hue);
// RSMods' HSL2RGB. Leaves out untouched when the hue falls outside its six sextants (the bug).
void HslToRgb(float h, float s, float l, float out[3]);
// The glow RSMods intends for `enabled`: true and out = HSL((hue - 4 + 360) % 360, 1, 0.5) when
// `custom` (RSMods' custom string colours are in effect) and the hue is 0..3; else false, out = glow.
bool CorrectGlow(bool custom, const float enabled[3], const float glow[3], float out[3]);
}  // namespace rs7rsmods

// Startup (Rs7Colour_Start): 1 when RSMods is the proxy with its custom string colours on and not shaded
// from the game's colours (rs7rsmods::CustomGlow); the GLW site is written and Rs7_CallIn_GlowColour
// corrects only then.
extern "C" __declspec(dllexport) uintptr_t g_rs7_glow_fix;
// The glow colour pointer Rnd_StringRenderer_UpdateGlow uses for string `s` (< 6) of colour manager `mgr`:
// a corrected copy of RSMods' glow (see above), else `glow` itself. Render thread.
extern "C" const float* __cdecl Rs7_GlowFixPtr(const float* glow, const uint8_t* mgr, uint32_t s);
extern "C" __declspec(dllexport) void __cdecl Rs7_Mid_GLW(Rs7Mid::Context& c);
