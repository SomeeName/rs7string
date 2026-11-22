#pragma once
#include "platform/sites.h"
#include <cstddef>
#include <cstdint>

// Plain (unencrypted) PSARC / SNG loading, rsxr.ini [psarc] plain (default 1). Any CDLC enabler
// still bypasses the SNG signature check (0x5582F0); this adds what the plain-PSARC enabler build
// (RSCDLCEnabler-TooManyCoresFix PR #1, PlainPsarc.cpp) did, as one unit of three sites (sites.json
// group "psarc", kRsxrPsarcSites):
//   PP_SNGD 0x558CD4  call SNG decrypt -> Rsxr_Stub_PP_SNGD: SNG flags 1 (plain) copies, else decrypts
//   PP_SNGF 0x558C1A  cmp [esp+68h],3 (SNG flags) -> also accepts 1
//   PP_TOC  0x8A3643  TOC decrypt block -> skipped when PSARC archiveFlags & 4 (encrypted TOC) is clear
//
// Coexistence: each site is Original (stock bytes), Ours (our jmp/call) or Foreign (anything else:
// an enabler with the PR patched first). Any Foreign -> all three are left alone ("provided by the CDLC
// enabler"); never a mix of ours and theirs. Each write is a compare-exchange against the stock bytes
// (Rsxr_CasCode); a failed exchange restores ours and re-classifies.
//
// Files: psarc.cpp (logic, tests/psarc), hooks.cpp (the stubs and the patching), registration.cpp (the hook
// table src/registry.cpp looks the sites up in), settings.cpp (the settings table).

// ---- startup (hooks.cpp)

// Startup, right after d3d9.dll appeared and the build was checked (the game reads PSARC TOCs from then on).
// Reads [psarc] plain and [debug] plain_delay_ms (test only: start that many ms late, at most 30000) from ini,
// then RsxrPlain_Start.
// `host` is what the app hands the unit: its hook lookup and the failure groups (platform/isolate.h).
struct RsxrHook;
struct RsxrGroupPolicy;

struct RsxrPlainHost {
    const RsxrHook* (*findHook)(const char* id);
    const RsxrGroupPolicy* groups;
};

void RsxrPlain_StartFromIni(const RsxrPlainHost& host, const wchar_t* ini, const RsxrSite* sites, int count,
                            uintptr_t textVa, size_t textSize);
// enabled = [psarc] plain. Logs every decision.
void RsxrPlain_Start(const RsxrPlainHost& host, bool enabled, const RsxrSite* sites, int count, uintptr_t textVa,
                     size_t textSize);
// Re-classifies the three sites and logs the state (an enabler that raced us may have overwritten ours).
void RsxrPlain_LogState(const char* when);
// Tests: the mid-hook entries taken from the unit's pool so far, and a lower pool limit (0: the whole pool; setting it
// also drops the per-site entry cache, so the next resolve takes new entries).
size_t RsxrPlain_MidPoolUsed();
void RsxrPlain_SetMidPoolLimit(size_t limit);
// Tests: the mid-hook entry of the resolved unit's site `id` (0: none).
uintptr_t RsxrPlain_MidEntry(const char* id);

// Who provides plain PSARC support, for the log and the overlay: g_rsxr_plain_psarc.
enum class RsxrPlainProvider : uintptr_t { Unknown, Off, Ours, Enabler, Failed };
extern "C" __declspec(dllexport) uintptr_t g_rsxr_plain_psarc;

// The feature's hooks (registration.cpp), consumed by src/registry.cpp.
struct RsxrHookTable;
extern const RsxrHookTable kRsxrPsarcHooks;
// [psarc] plain for the overlay and the template (settings.cpp; RsxrPlain_StartFromIni reads it).
struct RsxrSetTable;
extern const RsxrSetTable kRsxrPsarcSettings;

// ---- logic (psarc.cpp): values in, values out, no Win32, no game state

enum class RsxrSiteState { Original, Ours, Foreign };
const char* Rsxr_SiteStateName(RsxrSiteState s);
// live: the bytes at the site now (originalLen of them). original: the stock bytes. ours: what we write
// (oursLen <= originalLen; the bytes after it stay stock).
RsxrSiteState Rsxr_ClassifySite(const uint8_t* live, const uint8_t* original, size_t originalLen, const uint8_t* ours,
                                size_t oursLen);

enum class RsxrPlainDecision { Patch, Enabler, Already };
// Any Foreign -> Enabler; all Ours -> Already; else Patch (the Original ones).
RsxrPlainDecision RsxrPlain_Decide(const RsxrSiteState* states, size_t n);

enum class RsxrSngAction { Decrypt, Copy, Reject };
// The SNG at src - 8: u32 magic 0x4A, u32 flags. flags 1 (compressed, not encrypted): the payload after the
// 16-byte IV slot is copied (len - 16 bytes; Reject if len < 16 or out is smaller). Anything else: the
// game's decrypt.
RsxrSngAction RsxrPlain_SngAction(uint32_t magic, uint32_t flags, uint32_t len, uint32_t outLen);

const char* RsxrPlain_ProviderName(RsxrPlainProvider p);
