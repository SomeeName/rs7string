// C-lite (features/clite/clite.h). Game-thread mid-hooks GKC / GKE / GKM / GKL / GKT plus one
// log-only watcher thread that times guitar rebuilds (holder change -> load state 3).
#include "features/clite/clite.h"
#include "features/headstock/headstock.h"
#include "features/nslot/nslot.h"
#include "features/publish/publish.h"
#include "features/string7/string7.h"
#include "platform/log.h"
#include "platform/midhook.h"
#include <windows.h>
#include <intrin.h>

uintptr_t g_rs7_clite = 0;
uintptr_t g_rs7_clite_desired = 6;
uintptr_t g_rs7_clite_mgr = 0;

unsigned rs7clite::Desired(bool has6, bool has7, bool eight) {
    return !has6 ? 6u : has7 && eight ? 8u : 7u;
}

unsigned rs7clite::PegCount(unsigned n, bool nativePegset) {
    return n >= 7 ? n : nativePegset ? 7u : 6u;
}

unsigned rs7clite::BuildCountFor(unsigned desired, unsigned player) {
    return player == 0 && (desired == 7 || desired == 8) ? desired : 6u;
}

bool rs7clite::NormaliseSlot1(uint32_t ret, unsigned slot1Count) {
    return ModeEntry(ret) != nullptr && (slot1Count == 7 || slot1Count == 8);
}

bool rs7clite::AllowedCaller(uint32_t ret) {
    return ret == kRetSetFromManifest;
}

const char* rs7clite::ModeEntry(uint32_t ret) {
    switch (ret) {
        case 0x60726C:
        case 0x60C7D5:
            return "Guitarcade";
        case 0x613928:
            return "Lessons";
        case 0x6DBD09:
        case 0x6DBD63:
        case 0x6F44BE:
        case 0x6F44FA:
            return "Session Mode";
        case 0x73D904:
            return "Technique hub";
        default:
            return nullptr;
    }
}

bool rs7clite::TunerMenu(uint32_t ret) {
    for (uint32_t r : kRetTunerMenu) {
        if (r == ret) {
            return true;
        }
    }

    return false;
}

unsigned rs7clite::RebuildTarget(uint32_t ret, uint32_t player, unsigned want, unsigned was, bool tunerLive,
                                 bool songLive) {
    if (player != 0 || tunerLive) {
        return 0;
    }
    if (AllowedCaller(ret)) {
        return want != was ? want : 0u;
    }
    if (TunerMenu(ret)) {
        return !songLive && want != was ? want : 0u;
    }
    if (ModeEntry(ret) && want == 6 && was != 6) {
        return 6;
    }

    return 0;
}

unsigned rs7clite::TunersHolding(uintptr_t mgr, uintptr_t* guitar) {
    if (!mgr) {
        return 0;
    }

    unsigned n = 0;

    __try {
        for (unsigned i = 0; i < kTunerSlotCount; ++i) {
            const uintptr_t t = *reinterpret_cast<const uintptr_t*>(mgr + kTunerMgrSlots + i * 4);
            const uintptr_t g = t ? *reinterpret_cast<const uintptr_t*>(t + kTunerGuitar) : 0;
            if (!g) {
                continue;
            }

            if (!n && guitar) {
                *guitar = g;
            }
            ++n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return n ? n : 1u;  // unreadable: treat as held (no rebuild is the safe side)
    }

    return n;
}

namespace {
using namespace rs7clite;
bool g_eight = false;
volatile long g_lines = 0;
volatile LONG64 g_selQpc = 0;  // QPC of the last GKT rebuild request (watcher: "since selection")
volatile long g_selCount = 0;
volatile uint32_t g_loadPlayer =
    0;  // GKM: the player of the RebuildIfProfileChanged call whose loader creates the profile (0 = unknown / player 0)
volatile long g_rebuilds = 0;  // GKT rebuilds finished (EnsureForPlayer -> 1); the rebuilt guitar may reuse the address
LARGE_INTEGER g_freq{};

bool Upto(long cap) {
    return _InterlockedIncrement(&g_lines) <= cap;
}

double MsSince(LONG64 t0) {
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);

    return g_freq.QuadPart ? double(now.QuadPart - t0) * 1000.0 / double(g_freq.QuadPart) : 0.0;
}

LONG64 Now() {
    LARGE_INTEGER n;

    QueryPerformanceCounter(&n);

    return n.QuadPart;
}

template <class T>
T Rd(uintptr_t p) {
    return *reinterpret_cast<const T*>(p);
}

// The manifest key on the Tuning JSON object (game vtable as Play_Tuning_ReadStringOffsets 0x5B84DC).
using JsonFind = void*(__thiscall*)(void* self, const char* name);
using JsonNum = uint8_t*(__thiscall*)(void* self);

bool HasKey(void* obj, const char* key) {
    __try {
        if (!obj) {
            return false;
        }

        void** vt = *static_cast<void***>(obj);
        void* node = reinterpret_cast<JsonFind>(vt[0x14 / 4])(obj, key);
        if (!node) {
            return false;
        }

        void** nvt = *static_cast<void***>(node);
        uint8_t* num = reinterpret_cast<JsonNum>(nvt[0x50 / 4])(node);
        if (!num) {
            return false;
        }

        const double d = *reinterpret_cast<const double*>(num + 8);

        return d > -25.0 && d < 25.0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

struct Snap {
    uintptr_t holder = 0, guitar = 0;
    int state = -1;
    int count = -1;
    int type = -1;
};

Snap Read(uintptr_t mgr, unsigned slot) {
    Snap s;

    __try {
        s.holder = Rd<uintptr_t>(mgr + kMgrSlots + slot * 4);
        if (!s.holder) {
            return s;
        }

        s.guitar = Rd<uintptr_t>(s.holder + kHolderGuitar);
        const uintptr_t st = Rd<uintptr_t>(s.holder + kHolderState);

        s.state = st ? Rd<int>(st + kStateReady) : -1;
        const uintptr_t prof = Rd<uintptr_t>(s.holder + kHolderProfile);

        if (prof) {
            s.count = Rd<uint8_t>(prof + kProfileCount);
            s.type = Rd<int>(prof + kProfileType);
        }

        if (s.guitar) {  // the built InstrumentInfo's count (info +0Bh), when available
            const uintptr_t info = Rd<uintptr_t>(s.guitar + kGuitarInfo);

            if (info) {
                s.count = s.count * 100 + Rd<uint8_t>(info + 0x0B);  // profile*100 + info (log only)
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.state = -2;
    }

    return s;
}

DWORD WINAPI Watcher(void*) {
    Snap last[2];
    LONG64 born[2] = {0, 0};
    bool readyLogged[2] = {true, true};
    bool slowLogged[2] = {true, true};
    long seenRebuilds = 0;

    for (;;) {
        Sleep(5);
        const uintptr_t mgr = g_rs7_clite_mgr;
        if (!mgr || !g_rs7_clite) {
            continue;
        }

        // A GKT rebuild usually reuses the old guitar's addresses (rc-final3 run1: guitar 1BA645E0 for every build),
        // so a finished rebuild is a new guitar for the readiness log even when holder and guitar compare equal.
        const long rebuilds = g_rebuilds;
        const bool rebuilt = rebuilds != seenRebuilds;

        seenRebuilds = rebuilds;

        for (unsigned i = 0; i < 2; ++i) {
            const Snap s = Read(mgr, i);
            // The identity of a rebuild is the guitar object (Rnd_Guitar address), not the holder or the profile: a
            // holder (and its profile) can stay while the guitar behind it is built anew.
            const bool holderNew = s.holder != last[i].holder;
            const bool guitarNew = (s.guitar && s.guitar != last[i].guitar) || (rebuilt && i == 0 && s.guitar);

            if ((holderNew || guitarNew) && s.holder) {
                slowLogged[i] = false;
            }

            if (s.holder && !readyLogged[i] && !slowLogged[i] && s.state != 3 && MsSince(born[i]) > 10000.0) {
                slowLogged[i] = true;  // diagnostic: a guitar that does not finish loading holds the song in state 1

                if (Upto(400)) {
                    Rs7Log("clite: slot %u guitar %08X NOT ready 10 s after it appeared (state %d, count "
                           "profile*100+info %d)",
                           i, unsigned(s.guitar), s.state, s.count);
                }
            }

            if (holderNew || guitarNew) {
                if (holderNew || rebuilt || !born[i]) {
                    born[i] = Now();
                }
                readyLogged[i] = false;
                const LONG64 sel = g_selQpc;

                if (Upto(400)) {
                    Rs7Log("clite: slot %u %s %08X (guitar %08X, profile type %d, count profile*100+info %d, "
                           "state %d)%s%.1f ms after the last clite request",
                           i, holderNew ? "new guitar holder" : "new guitar in holder", unsigned(s.holder),
                           unsigned(s.guitar), s.type, s.count, s.state, sel ? ", " : " - ", sel ? MsSince(sel) : 0.0);
                }
            }

            if (s.holder && !readyLogged[i] && s.state == 3) {
                readyLogged[i] = true;
                const LONG64 sel = g_selQpc;

                if (Upto(400)) {
                    Rs7Log("clite: slot %u guitar %08X READY (state 3) %.1f ms after the holder appeared%s%.1f ms "
                           "after the "
                           "last clite request; count profile*100+info %d, type %d",
                           i, unsigned(s.guitar), MsSince(born[i]), sel ? ", " : " - ", sel ? MsSince(sel) : 0.0,
                           s.count, s.type);
                }
            }

            last[i] = s;
        }
    }
}
}  // namespace

void Rs7Clite_Configure(bool requested) {
    g_rs7_clite = requested && g_rs7_native_publish ? 1 : 0;
    Rs7Log("clite: %s", g_rs7_clite ? "guitar rebuilt with the arrangement's string count at selection (GK* sites; "
                                      "PSQ1 replaced by GKC)"
                                    : "off: a native layout or the native ports are unavailable");
}

void Rs7Clite_Start(bool eight) {
    g_eight = eight;
    QueryPerformanceFrequency(&g_freq);
    if (HANDLE h = CreateThread(nullptr, 0, &Watcher, nullptr, 0, nullptr)) {
        CloseHandle(h);
    }
    Rs7Log("clite: ON - 7-string arrangements rebuild the guitar with %s strings through the stock "
           "profile-changed path; rebuild timing watcher started",
           eight ? "7 (8 with Rs7String7)" : "7");
}

// GKC 0x593CC0: the guitar branch of Rnd_GuitarProfile_Load (ESI = profile). Replaces both stores.
extern "C" void __cdecl Rs7_Mid_GKC(Rs7Mid::Context& c) {
    // Review F4: the desired count is player 0's; the profile of any other player is built with six strings.
    const unsigned n = BuildCountFor(unsigned(g_rs7_clite_desired), g_loadPlayer);

    *reinterpret_cast<uint8_t*>(c.esi + kProfileCount) = uint8_t(n);
    // Peg pool request (+3Ah): PSQ1's 7 with the native PegSet (GKC replaces PSQ1), the count when larger.
    const uint32_t peg = PegWord(PegCount(n, g_rs7_native_pegset != 0));

    *reinterpret_cast<uint32_t*>(c.esi + 0x3A) = peg;
    c.jump = kGKCResume;
    static unsigned lastLogged = 0;

    if (n != lastLogged && Upto(400)) {
        lastLogged = n;
        Rs7Log("clite: guitar profile %08X loaded with %u strings (peg word %08X, player %u)", c.esi, n, peg,
               unsigned(g_loadPlayer));
    }
}

// GKE 0x596341: Rnd_GuitarProfile_Equals before the +13h compare (ESI / EDI the two profiles).
extern "C" void __cdecl Rs7_Mid_GKE(Rs7Mid::Context& c) {
    const uint8_t a = *reinterpret_cast<const uint8_t*>(c.esi + kProfileCount);
    const uint8_t b = *reinterpret_cast<const uint8_t*>(c.edi + kProfileCount);
    if (a == b) {
        return;
    }

    c.jump = kGKENotEqual;
    if (Upto(400)) {
        Rs7Log("clite: profiles differ in the string count (%u vs %u) -> guitar rebuild", a, b);
    }
}

// GKL 0x77CE9F in GuitarBindings::GetGuitarStringCount 0x77CE50 (the Lua binding; no native caller): ESI = the
// call object, ECX = 6 (guitar) or 4 (bass) from `lea ecx,[edi+5]` / `mov ecx,4`, [esp+0Ch] = the player index it
// resolved the class of; the next two instructions store ECX as the Lua result. A guitar built with 7/8 strings
// answers 7/8 (HYPOTHESIS: Lua's tuner / diagram code loops over this number).
unsigned rs7clite::BuildCountOf(uintptr_t mgr, unsigned player) {
    if (!mgr || player > 1) {
        return 6;
    }

    __try {
        const uintptr_t holder = Rd<uintptr_t>(mgr + kMgrSlots + player * 4);
        const uintptr_t prof = holder ? Rd<uintptr_t>(holder + kHolderProfile) : 0;
        if (!prof || Rd<int>(prof + kProfileType) != 1) {
            return 6;
        }

        const unsigned n = Rd<uint8_t>(prof + kProfileCount);

        return n == 7 || n == 8 ? n : 6u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 6;
    }
}

extern "C" void __cdecl Rs7_Mid_GKL(Rs7Mid::Context& c) {
    if (!g_rs7_clite || c.ecx != 6) {
        return;
    }

    const unsigned n = BuildCountOf(g_rs7_clite_mgr, *reinterpret_cast<const uint32_t*>(c.esp + 0x0C));

    if (n != 6) {
        c.ecx = n;
    }
}

// GKM 0x595ECB: Rnd_Guitar_RebuildIfProfileChanged, EBX = guitar manager.
extern "C" void __cdecl Rs7_Mid_GKM(Rs7Mid::Context& c) {
    // The loader call right after this site creates the profile GKC writes; [EBP+0Ch] is the player it is for. The
    // value stays until the next call: a stale 1 makes the next load six strings (the safe side), a stale 0 is the old
    // behaviour.
    g_loadPlayer = *reinterpret_cast<const uint32_t*>(c.ebp + 0x0C) == 0 ? 0u : 1u;

    if (g_rs7_clite_mgr != c.ebx) {
        if (Upto(400)) {
            Rs7Log("clite: guitar manager %08X (was %08X)", c.ebx, unsigned(g_rs7_clite_mgr));
        }
        g_rs7_clite_mgr = c.ebx;
    }
}

// The game's own rebuild of `player`'s guitar through Rnd_Guitar_EnsureForPlayer (GKC supplies the count of its new
// profile).
static void RebuildPlayer(unsigned player, uintptr_t mgr, unsigned want) {
    const LONG64 t0 = Now();

    g_selQpc = t0;
    _InterlockedIncrement(&g_selCount);
    // No SEH around the game's rebuild: a fault half way through would leave a half-built guitar (old one
    // destroyed, new one not attached) that the game would then keep using. It goes to the process' exception
    // handling (the game's crash handler / our crash log) and the cause is this switch: C-lite=0.
    uintptr_t before[2] = {};

    for (unsigned i = 0; i < 2; ++i) {
        before[i] = Read(mgr, i).guitar;
    }

    const int r = reinterpret_cast<EnsureFn>(kEnsureForPlayer)(player, mgr);

    if (r == 1) {
        Rs7Publish_GridForgetAll();  // a new build: no wait deadline or failed-neck mark of the old guitar (review F3)

        for (unsigned i = 0; i < 2; ++i) {
            if (const uintptr_t h = Rd<uintptr_t>(mgr + kMgrSlots + i * 4)) {
                Attach(h);
            }
        }

        _InterlockedIncrement(&g_rebuilds);
        // Every per-guitar state of a guitar that is gone now (Rs7_S7Free from the native GuitarStrings free normally
        // did it while the game released it; this also covers a release that did not pass it). A rebuilt guitar at the
        // SAME address keeps its fresh state: the free ran for the old object before the new one was built there.
        uintptr_t after[2] = {};

        for (unsigned i = 0; i < 2; ++i) {
            after[i] = Read(mgr, i).guitar;
        }

        for (unsigned i = 0; i < 2; ++i) {
            if (before[i] && before[i] != after[0] && before[i] != after[1]) {
                Rs7_GuitarGone(reinterpret_cast<void*>(before[i]));
            }
        }
    }

    if (Upto(400)) {
        Rs7Log("clite: EnsureForPlayer(%u) for %u strings -> %d (%s) in %.2f ms on the game thread", player, want, r,
               r == 1   ? "rebuilt"
               : r == 0 ? "kept"
                        : "?",
               MsSince(t0));
    }
}

// RebuildPlayer from the GKT mid-hook (full mode): the game's rebuild runs with the game's x87 control word. A function
// of its own: GKT uses __try, which allows no object with a destructor.
static void RebuildPlayerFcw(const Rs7Mid::Context& c, unsigned player, uintptr_t mgr, unsigned want) {
    const Rs7Mid::GameFcw fcw(c);

    RebuildPlayer(player, mgr, want);
}

// GKT 0x5B6098: after Play_TargetTuningStore_Set in Play_Tuning_SetPlayerTargetTuning (ESI player,
// [EBP+8] the Tuning JSON object, [EBP+4] the caller).
extern "C" void __cdecl Rs7_Mid_GKT(Rs7Mid::Context& c) {
    if (!g_rs7_clite) {
        return;
    }

    const uint32_t player = c.esi;
    const uint32_t ret = *reinterpret_cast<const uint32_t*>(c.ebp + 4);
    uint32_t ret2 = 0;

    __try {
        ret2 = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<const uint32_t*>(c.ebp) + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ret2 = 0;
    }

    void* obj = *reinterpret_cast<void**>(c.ebp + 8);
    const bool has6 = HasKey(obj, "Rs7String6");
    const bool has7 = has6 && HasKey(obj, "Rs7String7");
    unsigned want = Desired(has6, has7, g_eight);
    const unsigned was = unsigned(g_rs7_clite_desired);
    const char* mode = ModeEntry(ret);
    uintptr_t tunerGuitar = 0;
    const uintptr_t tunerMgr = *reinterpret_cast<const uintptr_t*>(kTunerMgrGlobal);
    const unsigned tuners = TunersHolding(tunerMgr, &tunerGuitar);
    const bool menu = TunerMenu(ret);
    const int songs = menu ? Rs7Ns_LiveSongs() : 0;
    const unsigned target = RebuildTarget(ret, player, want, was, tuners != 0, songs > 0);
    const bool songSelect = AllowedCaller(ret) && player == 0;

    if (!target && !tuners && songs > 0 && RebuildTarget(ret, player, want, was, false, false) && Upto(400)) {
        Rs7Log("clite: tuner menu store while %d song(s) are loaded - NO rebuild under a song; %u strings stay built",
               songs, was);
    }

    if (!target && tuners && RebuildTarget(ret, player, want, was, false, songs > 0) && Upto(400)) {
        Rs7Log("clite: %u live tuner(s) of manager %08X hold guitar %08X - NO rebuild while a tuner is open (it would "
               "update the freed guitar); %u strings stay built, the next listed store decides again",
               tuners, unsigned(tunerMgr), unsigned(tunerGuitar), was);
    }

    const char* why = songSelect    ? ""
                      : player != 0 ? " - player 1: no rebuild"
                      : mode        ? " - mode entry"
                      : menu        ? " - tuner menu (TuningOptionsMenu)"
                                    : " - not the song-select path / a mode entry: no rebuild";

    if (Upto(400)) {
        Rs7Log("clite: target tuning stored (player %u, caller %08X / %08X, tuning %p): Rs7String6 %d Rs7String7 %d -> "
               "%u strings (current %u)%s%s%s",
               player, ret, ret2, obj, has6, has7, want, was, why, mode && !songSelect ? ": " : "",
               mode && !songSelect ? mode : "");
    }

    const uintptr_t mgr = g_rs7_clite_mgr;
    // Review F4: a mode entry also normalises slot 1 (player 1 is never built with 7/8; GKC writes 6 for it, so this
    // only fires for a guitar that bypassed GKM). Its profile is built with 6 whatever the desired count.
    const bool norm1 = mgr && mode && NormaliseSlot1(ret, BuildCountOf(mgr, 1));
    if (!target && !norm1) {
        return;
    }

    if (target) {
        if (menu && Upto(400)) {
            Rs7Log(
                "clite: tuner menu tuning with %u strings on a guitar built with %u -> rebuild before the tuner opens",
                target, was);
        } else if (!songSelect && Upto(400)) {
            Rs7Log("clite: leaving song play for %s with a guitar built with %u strings -> rebuild with 6", mode, was);
        }

        want = target;

        if (!mgr) {
            if (Upto(400)) {
                Rs7Log("clite: no guitar manager seen yet - %u strings stay pending", want);
            }
        } else {
            g_rs7_clite_desired = want;
            RebuildPlayerFcw(c, player, mgr, want);
        }
    }

    if (norm1) {
        if (Upto(400)) {
            Rs7Log("clite: leaving song play for %s with player 1's guitar built with 7/8 strings -> rebuild with 6",
                   mode);
        }
        RebuildPlayerFcw(c, 1, mgr, 6);
    }
}

// GKH1 0x58E1BA (loop entry, EBX = info, [EBP-24h] = Headstock): replaces `mov al,[ebx+0Bh]; xor cl,cl`.
// GKH2 0x58E36D (loop end): replaces `mov edx,[ebp-40h]; mov al,[edx+0Bh]` (EDX = info); the stock `inc cl` follows.
// The next instructions (test al,al / cmp cl,al) set the flags themselves.
extern "C" void __cdecl Rs7_Mid_GKH1(Rs7Mid::Context& c) {
    const unsigned n = *reinterpret_cast<const uint8_t*>(c.ebx + 0x0B);
    const unsigned use = HeadstockLoopCount(n);

    c.eax = (c.eax & ~0xFFu) | use;
    c.ecx &= ~0xFFu;
    c.jump = kGKH1Resume;

    if (use != n) {
        static volatile long lines = 0;

        if (_InterlockedIncrement(&lines) <= 12) {
            uintptr_t head = 0, past = 0;
            int flag6 = -1;

            __try {  // log only: what the stock loop would have used for slot 6 (past the 0x38-byte Headstock)
                head = *reinterpret_cast<const uintptr_t*>(c.ebp - 0x24);

                if (head) {
                    flag6 = *reinterpret_cast<const uint8_t*>(head + 0x1E);
                    past = *reinterpret_cast<const uintptr_t*>(head + 0x38);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }

            Rs7Log("clite: headstock %08X string segments: info %08X count %u -> loop over %u (stock slot 6 would have "
                   "read loaded[6]=%d at +1Eh and params[6]=%08X at +38h, past the object)",
                   unsigned(head), c.ebx, n, use, flag6, unsigned(past));
        }
    }
}

extern "C" void __cdecl Rs7_Mid_GKH2(Rs7Mid::Context& c) {
    const uint32_t info = *reinterpret_cast<const uint32_t*>(c.ebp - 0x40);

    c.edx = info;
    c.eax = (c.eax & ~0xFFu) | HeadstockLoopCount(*reinterpret_cast<const uint8_t*>(info + 0x0B));
    c.jump = kGKH2Resume;
}
