#pragma once
#include "core/sites.h"
#include <cstddef>
#include <cstdint>

// Plain (unencrypted) PSARC / SNG loading, rs7string.ini [psarc] plain (default 1). Any CDLC enabler
// still bypasses the SNG signature check (0x5582F0); this adds what the plain-PSARC enabler build
// (RSCDLCEnabler-TooManyCoresFix PR #1, PlainPsarc.cpp) did, as one unit of three sites (sites.json
// group "psarc", kRs7PsarcSites):
//   PP_SNGD 0x558CD4  call SNG decrypt -> Rs7_Stub_PP_SNGD: SNG flags 1 (plain) copies, else decrypts
//   PP_SNGF 0x558C1A  cmp [esp+68h],3 (SNG flags) -> also accepts 1
//   PP_TOC  0x8A3643  TOC decrypt block -> skipped when PSARC archiveFlags bit 4 (encrypted TOC) is clear
//
// Coexistence: each site is Original (stock bytes), Ours (our jmp/call) or Foreign (anything else:
// an enabler with the PR patched first). Any Foreign -> all three are left alone ("provided by the CDLC
// enabler"); never a mix of ours and theirs. Each write is a compare-exchange against the stock bytes
// (Rs7_CasCode); a failed exchange restores ours and re-classifies.

enum class Rs7SiteState { Original, Ours, Foreign };
const char* Rs7_SiteStateName(Rs7SiteState s);
// live: the bytes at the site now (originalLen of them). original: the stock bytes. ours: what we write
// (oursLen <= originalLen; the bytes after it stay stock).
Rs7SiteState Rs7_ClassifySite(const uint8_t* live, const uint8_t* original, size_t originalLen, const uint8_t* ours,
                              size_t oursLen);

enum class Rs7PlainDecision { Patch, Enabler, Already };
// Any Foreign -> Enabler; all Ours -> Already; else Patch (the Original ones).
Rs7PlainDecision Rs7Plain_Decide(const Rs7SiteState* states, size_t n);

enum class Rs7SngAction { Decrypt, Copy, Reject };
// The SNG at src - 8: u32 magic 0x4A, u32 flags. flags 1 (compressed, not encrypted): the payload after the
// 16-byte IV slot is copied (len - 16 bytes; Reject if len < 16 or out is smaller). Anything else: the
// game's decrypt.
Rs7SngAction Rs7Plain_SngAction(uint32_t magic, uint32_t flags, uint32_t len, uint32_t outLen);

// Who provides plain PSARC support, for the log and the overlay: g_rs7_plain_psarc.
enum class Rs7PlainProvider : uintptr_t { Unknown, Off, Ours, Enabler, Failed };
extern "C" __declspec(dllexport) uintptr_t g_rs7_plain_psarc;
const char* Rs7Plain_ProviderName(Rs7PlainProvider p);

// Startup, right after d3d9.dll appeared and the build was checked (the game reads PSARC TOCs from then on).
// enabled = [psarc] plain. Logs every decision.
void Rs7Plain_Start(bool enabled, const Rs7Site* sites, int count, uintptr_t textVa, size_t textSize);
// Re-classifies the three sites and logs the state (an enabler that raced us may have overwritten ours).
void Rs7Plain_LogState(const char* when);
