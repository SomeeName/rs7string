// C-lite game glue: the mid-hooks GKC / GKB / GKE / GKL / GKM / GKT / GKH1 / GKH2. Decisions are in clite.cpp.
#include "features/clite/clite.h"
#include "features/clite/clite_internal.h"
#include "features/colours/colours.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "model/instrument.h"
#include "model/manifest.h"
#include "model/tuning.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include "platform/protect.h"
#include "platform/seh.h"
#include <windows.h>
#include <intrin.h>

using namespace rsxrclite;
using namespace rsxrclite::internal;

namespace rsxrclite::internal {
volatile uint32_t g_loadPlayer =
    0;  // GKM: the player of the RebuildIfProfileChanged call whose loader creates the profile (0 = unknown / player 0)
volatile long g_gkmFresh = 0;  // GKM ran since the last profile load (GKC / GKB); a load without it is logged
volatile long g_logNoGkm = 0;

// Rnd_Guitar_EnsureForPlayer 0x596030: ECX = player, [esp+4] = guitar manager, callee pops; EAX 1 = rebuilt.
using EnsureFn = int(__thiscall*)(uintptr_t player, uintptr_t mgr);

// 0x4B24F0: EAX = guitar holder (scene attach after a rebuild; the route buttons call it for both slots).
void Attach(uintptr_t holder) {
    uintptr_t fn = kGuitarAttach;

    __asm {
        mov eax, holder
        call fn
    }
}
}  // namespace rsxrclite::internal

void RsxrClite_Configure(bool requested) {
    g_rsxr_clite = requested && g_rsxr_native_publish ? 1 : 0;
    RsxrLog("clite: %s", g_rsxr_clite ? "guitar rebuilt with the arrangement's string count at selection (GK* sites; "
                                        "PSQ1 replaced by GKC)"
                                      : "off: a native layout or the native ports are unavailable");
}

void RsxrClite_Start() {
    StartWatcher();
    RsxrLog(
        "clite: ON - 7-string arrangements rebuild the guitar with 7 (8 with RsxrString7) strings through the stock "
        "profile-changed path; rebuild timing watcher started");
}

namespace {
// Co-op check (#171, HYPOTHESIS): a profile load that no GKM announced uses the previous load's player. Logged so a
// game run shows whether it happens before the player is cleared after each load.
void NoteLoad(const char* kind) {
    if (!_InterlockedExchange(&g_gkmFresh, 0) && RsxrLog_Upto(&g_logNoGkm, 4)) {
        RsxrLog("clite: %s profile load without a GKM before it - player %u of the previous load used", kind,
                unsigned(g_loadPlayer));
    }
}
}  // namespace

// GKC 0x593CC0: the guitar branch of Rnd_GuitarProfile_Load (ESI = profile). Replaces both stores.
extern "C" void __cdecl Rsxr_Mid_GKC(RsxrMid::Context& c) {
    // The desired count of the player whose profile this is (GKM).
    NoteLoad("guitar");
    const unsigned n = BuildCountFor(unsigned(g_rsxr_clite_desired[g_loadPlayer & 1]), g_loadPlayer);

    *reinterpret_cast<uint8_t*>(c.esi + kProfileCount) = uint8_t(n);
    rsxrcolour::SetBassShift(g_loadPlayer & 1, false);  // a guitar: string 4 is its B string
    // Peg pool request (+3Ah): PSQ1's 7 with the native PegSet (GKC replaces PSQ1), the count when larger.
    const uint32_t peg = PegWord(PegCount(n, g_rsxr_native_pegset != 0));

    *reinterpret_cast<uint32_t*>(c.esi + 0x3A) = peg;
    c.jump = kGKCResume;
    static unsigned lastLogged = 0;

    if (n != lastLogged && RsxrLog_Upto(&g_lines, kLogCap)) {
        lastLogged = n;
        RsxrLog("clite: guitar profile %08X loaded with %u strings (peg word %08X, player %u)", c.esi, n, peg,
                unsigned(g_loadPlayer));
    }
}

// GKB 0x593D33: the bass branch of Rnd_GuitarProfile_Load (ESI = profile). Replaces both stores.
// X_TBASS: the tuner's bass open MIDI table Play_TunerBassBaseMidi_i32x6 (0x1199CA4, 28 33 38 43 0 0). Its [4] / [5]
// are 0, so the tuner gave a 5/6-string bass's low B / high C open MIDI 0 + offset (peg letter C, tolerance measured
// from MIDI 0). Filled with B0 23 / C3 48 once, by the first bass built with more strings; a 4-string bass never reads
// them (the tuner's loops end at the string count).
extern "C" __declspec(dllexport) uintptr_t g_rsxr_tuner_bassbase = 0;

namespace {
BOOL CALLBACK FillBassBase(PINIT_ONCE, PVOID param, PVOID*) {
    int32_t* t = static_cast<int32_t*>(param);

    if (!t) {
        return false;
    }
    if (t[4] == rsxr::kBassOpenMidi[4] && t[5] == rsxr::kBassOpenMidi[5]) {
        return true;
    }

    DWORD old = 0;

    if (!Rsxr_Protect(t + 4, 8, PAGE_READWRITE, &old)) {
        try {
            RsxrLog("clite: tuner bass open MIDI table %p NOT filled (protect failed)", static_cast<void*>(t));
        } catch (...) {
            // A diagnostic must not leave InitOnceExecuteOnce pending.
        }

        return false;
    }

    t[4] = rsxr::kBassOpenMidi[4];
    t[5] = rsxr::kBassOpenMidi[5];
    Rsxr_Protect(t + 4, 8, old, &old);

    try {
        RsxrLog("clite: tuner bass open MIDI table %p [4]/[5] = %d / %d (low B, high C)", static_cast<void*>(t),
                rsxr::kBassOpenMidi[4], rsxr::kBassOpenMidi[5]);
    } catch (...) {
        // The table is ready even if its diagnostic failed.
    }

    return true;
}
}  // namespace

extern "C" void __cdecl Rsxr_Mid_GKB(RsxrMid::Context& c) {
    NoteLoad("bass");
    unsigned n = BassCountFor(unsigned(g_rsxr_clite_bass[g_loadPlayer & 1]), g_loadPlayer);

    if (n > 4) {
        static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

        if (!InitOnceExecuteOnce(&once, FillBassBase, reinterpret_cast<void*>(g_rsxr_tuner_bassbase), nullptr)) {
            n = 4;
        }
    }

    *reinterpret_cast<uint8_t*>(c.esi + kProfileCount) = uint8_t(n);
    rsxrcolour::SetBassShift(g_loadPlayer & 1, n > 4);  // the low B's own colour slot for this player (#170)
    // One peg per string; peg 5 / 6 sit on the edited model's Locator_Peg_5 / 6 (rsxr_alt, features/headstock).
    *reinterpret_cast<uint32_t*>(c.esi + 0x3A) = PegWord(n);
    c.jump = kGKBResume;
    static unsigned lastLogged = 4;

    if (n != lastLogged && RsxrLog_Upto(&g_lines, kLogCap)) {
        lastLogged = n;
        RsxrLog("clite: bass profile %08X loaded with %u strings (player %u)", c.esi, n, unsigned(g_loadPlayer));
    }
}

// GKE 0x596341: Rnd_GuitarProfile_Equals before the +13h compare (ESI / EDI the two profiles).
extern "C" void __cdecl Rsxr_Mid_GKE(RsxrMid::Context& c) {
    const uint8_t a = *reinterpret_cast<const uint8_t*>(c.esi + kProfileCount);
    const uint8_t b = *reinterpret_cast<const uint8_t*>(c.edi + kProfileCount);
    if (a == b) {
        return;
    }

    c.jump = kGKENotEqual;
    if (RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: profiles differ in the string count (%u vs %u) -> guitar rebuild", a, b);
    }
}

// GKL 0x77CE9F in GuitarBindings::GetGuitarStringCount 0x77CE50 (the Lua binding; no native caller): ESI = the
// call object, ECX = 6 (guitar) or 4 (bass) from `lea ecx,[edi+5]` / `mov ecx,4`, [esp+0Ch] = the player index it
// resolved the class of; the next two instructions store ECX as the Lua result. A guitar built with 7/8 strings
// answers 7/8 (HYPOTHESIS: Lua's tuner / diagram code loops over this number).
extern "C" void __cdecl Rsxr_Mid_GKL(RsxrMid::Context& c) {
    if (!g_rsxr_clite || c.ecx != 6) {
        return;
    }

    const unsigned n = BuildCountOf(g_rsxr_clite_mgr, *reinterpret_cast<const uint32_t*>(c.esp + 0x0C));

    if (n != 6) {
        c.ecx = n;
    }
}

// GKM 0x595ECB: Rnd_Guitar_RebuildIfProfileChanged, EBX = guitar manager.
extern "C" void __cdecl Rsxr_Mid_GKM(RsxrMid::Context& c) {
    // The loader call right after this site creates the profile GKC writes; [EBP+0Ch] is the player it is for. The
    // value stays until the next call: a stale 1 makes the next load six strings (the safe side), a stale 0 is the old
    // behaviour.
    g_loadPlayer = *reinterpret_cast<const uint32_t*>(c.ebp + 0x0C) == 0 ? 0u : 1u;
    _InterlockedExchange(&g_gkmFresh, 1);

    if (g_rsxr_clite_mgr != c.ebx) {
        if (RsxrLog_Upto(&g_lines, kLogCap)) {
            RsxrLog("clite: guitar manager %08X (was %08X)", c.ebx, unsigned(g_rsxr_clite_mgr));
        }
        g_rsxr_clite_mgr = c.ebx;
    }
}

// The game's own rebuild of `player`'s guitar through Rnd_Guitar_EnsureForPlayer (GKC supplies the count of its new
// profile).
static void RebuildPlayer(unsigned player, uintptr_t mgr, unsigned want) {
    const LONG64 t0 = Now();

    g_selQpc = t0;
    // No SEH around the game's rebuild: a fault half way through would leave a half-built guitar (old one
    // destroyed, new one not attached) that the game would then keep using. It goes to the process' exception
    // handling (the game's crash handler / our crash log) and the cause is this switch: C-lite=0.
    uintptr_t before[2] = {};

    for (unsigned i = 0; i < 2; ++i) {
        before[i] = Read(mgr, i).guitar;
    }

    const int r = reinterpret_cast<EnsureFn>(kEnsureForPlayer)(player, mgr);

    if (r == 1) {
        for (unsigned i = 0; i < 2; ++i) {
            if (const uintptr_t h = UncheckedRead<uintptr_t>(mgr + kMgrSlots + i * 4)) {
                Attach(h);
            }
        }

        _InterlockedIncrement(&g_rebuilds[player & 1]);
        // Every per-guitar state of a guitar that is gone now (Rsxr_S7Free from the native GuitarStrings free normally
        // did it while the game released it; this also covers a release that did not pass it). A rebuilt guitar at the
        // SAME address keeps its fresh state: the free ran for the old object before the new one was built there.
        uintptr_t after[2] = {};

        for (unsigned i = 0; i < 2; ++i) {
            after[i] = Read(mgr, i).guitar;
        }

        for (unsigned i = 0; i < 2; ++i) {
            if (before[i] && before[i] != after[0] && before[i] != after[1]) {
                Rsxr_GuitarGone(reinterpret_cast<void*>(before[i]));
            }
        }
    }

    if (RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: EnsureForPlayer(%u) for %u strings -> %d (%s) in %.2f ms on the game thread", player, want, r,
                r == 1   ? "rebuilt"
                : r == 0 ? "kept"
                         : "?",
                MsSince(t0));
    }
}

// RebuildPlayer from the GKT mid-hook (full mode): the game's rebuild runs with the game's x87 control word. A function
// of its own: GKT uses __try, which allows no object with a destructor.
static void RebuildPlayerFcw(const RsxrMid::Context& c, unsigned player, uintptr_t mgr, unsigned want) {
    const RsxrMid::GameFcw fcw(c);

    RebuildPlayer(player, mgr, want);
}

// GKT's extended-bass part: the bass build count from RsxrString4 / RsxrString5, rebuilt by the same rules (stock 4).
// The count is stored before any rebuild (GKB reads it in the profile load); `guitarRebuilds`: GKT rebuilds anyway.
static void StoreBass(const RsxrMid::Context& c, void* obj, uint32_t ret, uint32_t player, uintptr_t mgr, bool tuners,
                      bool songs, bool guitarRebuilds) {
    const bool has4 = rsxr::HasOffset(obj, "RsxrString4");
    const unsigned want = BassDesired(has4, has4 && rsxr::HasOffset(obj, "RsxrString5"));
    const unsigned was = unsigned(g_rsxr_clite_bass[player & 1]);
    const unsigned target = RebuildTarget(ret, player, want, was, tuners, songs, 4);

    if (!target) {
        return;
    }

    if (RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: bass count %u -> %u (RsxrString4 %d)%s", was, target, has4, mgr ? "" : " - pending");
    }

    g_rsxr_clite_bass[player & 1] = target;

    if (mgr) {
        if (!guitarRebuilds) {
            RebuildPlayerFcw(c, player, mgr, target);
        }
    }
}

namespace {
// What GKT saw for one target tuning store (log only).
struct GktStore {
    uint32_t player, ret, ret2;
    const void* obj;
    bool has6, has7, menu, songSelect;
    unsigned want, was, tuners;
    const char* mode;
    uintptr_t tunerMgr, tunerGuitar;
    int songs;
};

// GKT's log of a store: what was stored, and why it does not rebuild when a loaded song or a live tuner holds back a
// rebuild the store would otherwise do.
void LogStore(const GktStore& s, unsigned target) {
    if (!target && !s.tuners && s.songs > 0 && RebuildTarget(s.ret, s.player, s.want, s.was, false, false) &&
        RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: tuner menu store while %d song(s) are loaded - NO rebuild under a song; %u strings stay built",
                s.songs, s.was);
    }

    if (!target && s.tuners && RebuildTarget(s.ret, s.player, s.want, s.was, false, s.songs > 0) &&
        RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: %u live tuner(s) of manager %08X hold guitar %08X - NO rebuild while a tuner is open (it would "
                "update the freed guitar); %u strings stay built, the next listed store decides again",
                s.tuners, unsigned(s.tunerMgr), unsigned(s.tunerGuitar), s.was);
    }

    const char* why = s.songSelect   ? ""
                      : s.player > 1 ? " - unknown player: no rebuild"
                      : s.mode       ? " - mode entry"
                      : s.menu       ? " - tuner menu (TuningOptionsMenu)"
                                     : " - not the song-select path / a mode entry: no rebuild";
    const bool named = s.mode && !s.songSelect;

    if (RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog(
            "clite: target tuning stored (player %u, caller %08X / %08X, tuning %p): RsxrString6 %d RsxrString7 %d -> "
            "%u strings (current %u)%s%s%s",
            s.player, s.ret, s.ret2, s.obj, s.has6, s.has7, s.want, s.was, why, named ? ": " : "", named ? s.mode : "");
    }
}
}  // namespace

// GKT 0x5B6098: after Play_TargetTuningStore_Set in Play_Tuning_SetPlayerTargetTuning (ESI player,
// [EBP+8] the Tuning JSON object, [EBP+4] the caller).
extern "C" void __cdecl Rsxr_Mid_GKT(RsxrMid::Context& c) {
    if (!g_rsxr_clite) {
        return;
    }

    GktStore s{};

    s.player = c.esi;
    s.ret = *reinterpret_cast<const uint32_t*>(c.ebp + 4);

    __try {
        s.ret2 = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<const uint32_t*>(c.ebp) + 4);
    } __except (Rsxr_FaultFilter(GetExceptionCode())) {
        s.ret2 = 0;
    }

    void* obj = *reinterpret_cast<void**>(c.ebp + 8);

    s.obj = obj;
    s.has6 = rsxr::HasOffset(obj, "RsxrString6");
    s.has7 = s.has6 && rsxr::HasOffset(obj, "RsxrString7");
    s.want = Desired(s.has6, s.has7);
    s.was = unsigned(g_rsxr_clite_desired[s.player & 1]);
    s.mode = ModeEntry(s.ret);
    s.tunerMgr = *reinterpret_cast<const uintptr_t*>(kTunerMgrGlobal);
    s.tuners = TunersHolding(s.tunerMgr, &s.tunerGuitar);
    s.menu = TunerMenu(s.ret);
    s.songs = s.menu ? RsxrNs_LiveSongs() : 0;
    s.songSelect = AllowedCaller(s.ret) && s.player <= 1;

    const uint32_t player = s.player;
    const unsigned target = RebuildTarget(s.ret, player, s.want, s.was, s.tuners != 0, s.songs > 0);

    LogStore(s, target);

    const uintptr_t mgr = g_rsxr_clite_mgr;
    // A mode entry stores player 0's tuning only: player 1's guitar built with 7/8 or bass with 5/6 strings for a co-op
    // song goes back to the stock count too. Same guards as every other rebuild: never while a player tuner holds a
    // guitar (it would update the freed guitar) or a song is loaded.
    const bool slot1Wide =
        mgr && s.mode && player == 0 && (NormaliseSlot1(s.ret, BuildCountOf(mgr, 1)) || g_rsxr_clite_bass[1] != 4);
    const bool norm1 = slot1Wide && !s.tuners && s.songs == 0;

    if (slot1Wide && !norm1 && RsxrLog_Upto(&g_lines, kLogCap)) {
        RsxrLog("clite: player 1's wide guitar / bass NOT normalised on the %s entry (%u live tuner(s), %d song(s))",
                s.mode, s.tuners, s.songs);
    }

    StoreBass(c, obj, s.ret, player, mgr, s.tuners != 0, s.songs > 0, target != 0);

    if (!target && !norm1) {
        return;
    }

    if (target) {
        if (s.menu && RsxrLog_Upto(&g_lines, kLogCap)) {
            RsxrLog(
                "clite: tuner menu tuning with %u strings on a guitar built with %u -> rebuild before the tuner opens",
                target, s.was);
        } else if (!s.songSelect && RsxrLog_Upto(&g_lines, kLogCap)) {
            RsxrLog("clite: leaving song play for %s with a guitar built with %u strings -> rebuild with 6", s.mode,
                    s.was);
        }

        g_rsxr_clite_desired[player & 1] = target;

        if (!mgr) {
            if (RsxrLog_Upto(&g_lines, kLogCap)) {
                RsxrLog("clite: no guitar manager seen yet - %u strings stay pending", target);
            }
        } else {
            RebuildPlayerFcw(c, player, mgr, target);
        }
    }

    if (norm1) {
        if (RsxrLog_Upto(&g_lines, kLogCap)) {
            RsxrLog("clite: leaving song play for %s with player 1's guitar built with %u / bass with %u strings -> "
                    "rebuild with 6 / 4",
                    s.mode, BuildCountOf(mgr, 1), unsigned(g_rsxr_clite_bass[1]));
        }

        g_rsxr_clite_desired[1] = 6;
        g_rsxr_clite_bass[1] = 4;
        RebuildPlayerFcw(c, 1, mgr, 6);
    }
}

// GKH1 0x58E1BA (loop entry, EBX = info, [EBP-24h] = Headstock): replaces `mov al,[ebx+0Bh]; xor cl,cl`.
// GKH2 0x58E36D (loop end): replaces `mov edx,[ebp-40h]; mov al,[edx+0Bh]` (EDX = info); the stock `inc cl` follows.
// The next instructions (test al,al / cmp cl,al) set the flags themselves.
extern "C" void __cdecl Rsxr_Mid_GKH1(RsxrMid::Context& c) {
    const unsigned n = *reinterpret_cast<const uint8_t*>(c.ebx + rsxr::kInfoStrings);
    const unsigned use = HeadstockLoopCount(n, *reinterpret_cast<const uint8_t*>(c.ebx));

    c.eax = (c.eax & ~0xFFu) | use;
    c.ecx &= ~0xFFu;
    c.jump = kGKH1Resume;

    if (use != n) {
        static volatile long lines = 0;

        if (RsxrLog_Upto(&lines, 12)) {
            uintptr_t head = 0, past = 0;
            int flag6 = -1;

            __try {  // log only: what the stock loop would have used for slot 6 (past the 0x38-byte Headstock)
                head = *reinterpret_cast<const uintptr_t*>(c.ebp - 0x24);

                if (head) {
                    flag6 = *reinterpret_cast<const uint8_t*>(head + 0x1E);
                    past = *reinterpret_cast<const uintptr_t*>(head + 0x38);
                }
            } __except (Rsxr_FaultFilter(GetExceptionCode())) {
            }

            RsxrLog(
                "clite: headstock %08X string segments: info %08X count %u -> loop over %u (stock slot 6 would have "
                "read loaded[6]=%d at +1Eh and params[6]=%08X at +38h, past the object)",
                unsigned(head), c.ebx, n, use, flag6, unsigned(past));
        }
    }
}

extern "C" void __cdecl Rsxr_Mid_GKH2(RsxrMid::Context& c) {
    const uint32_t info = *reinterpret_cast<const uint32_t*>(c.ebp - 0x40);

    c.edx = info;
    c.eax = (c.eax & ~0xFFu) | HeadstockLoopCount(*reinterpret_cast<const uint8_t*>(info + rsxr::kInfoStrings),
                                                  *reinterpret_cast<const uint8_t*>(info));
    c.jump = kGKH2Resume;
}
