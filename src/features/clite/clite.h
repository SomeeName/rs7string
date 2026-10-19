#pragma once
#include <cstdint>

// C-lite (docs/ARCHITECTURE.md), the way 7-string support works; a member of the native 7-string unit (no ini switch
// since 1.0). The string count is a property of the guitar BUILD: a 7-string arrangement gets a guitar built
// with 7 strings through the game's own "profile changed -> rebuild" path (the one guitar <-> bass uses);
// nothing switches the count of an existing guitar at song setup, in the tuner or at song end. 7-string
// support needs C-lite AND the native publication ports (the native layouts); either one unavailable at startup =
// nothing patched (startup logs it). Feature group "C-lite" (model/groups.h), sites GKC /
// GKE / GKM / GKL / GKT:
//  - GKC 0x593CC0 in Rnd_GuitarProfile_Load 0x593B00 (guitar branch): `mov byte [esi+38h],6;
//    mov dword [esi+3Ah],19180006h` write the desired count instead; the peg word keeps PSQ1's 7 with the
//    native PegSet (GKC overlaps PSQ1 0x593CC7, which is not written with clite=1), the count when larger.
//  - GKE 0x596341 in Rnd_GuitarProfile_Equals 0x596290 (ESI / EDI the two profiles): profiles whose
//    +38h counts differ are "not equal" (jump to the stock not-equal exit 0x596558).
//  - GKM 0x595ECB in Rnd_Guitar_RebuildIfProfileChanged 0x595EA0 (EBX = guitar manager, [EBP+0Ch] = player): remembers
//    the manager for GKT's call and the player whose profile the loader call that follows creates (GKC writes 6 for
//    every player but 0, review F4).
//  - GKL 0x77CE9F in GetGuitarStringCount 0x77CE50 (Lua): the answer for a guitar is the build count.
//  - GKT 0x5B6098 in Play_Tuning_SetPlayerTargetTuning 0x5B6030 after Play_TargetTuningStore_Set
//    (ESI = player, [EBP+8] = the Tuning JSON): desired count from the manifest keys (Rs7String6 -> 7,
//    + Rs7String7 with [neck] eight -> 8, else 6); when it changed and the caller is the song-select
//    path (Play_SetPlayerTuningFromManifest), call Rnd_Guitar_EnsureForPlayer 0x596030 like the
//    route buttons 0x597C80 do (then 0x4B24F0 for both slots). From a mode entry (kRetModeEntries: Session Mode,
//    Guitarcade, Lessons, Technique hub) a guitar built with 7/8 is rebuilt with 6 the same way (door (b)).
//  - GKH1 0x58E1BA / GKH2 0x58E36D in Rnd_Headstock_LoadStringMeshes 0x58E090: the loop bound `mov al,[info+0Bh]`
//    (loop entry 0x58E1BA, EBX = info; loop end 0x58E370 after `mov edx,[ebp-40h]` = info) is the build count, but the
//    Headstock object is 0x38 bytes with six slots (+18h loaded[6], +20h params[6]): with 7 the stock loop read
//    params[6] at +38h (past the object) and called Rnd_SetStringShaderParams 0x5998F0 on it (CONFIRMED crash,
//    tests/rc-final3/crash2.mdmp: slot 6, "string_size_1", EAX = 04000400), and shifted the names (count - s: s 0 ->
//    string_size_7). The loop runs at most 6 (string_size_6..1 as on a six-string guitar); string_size_7 has its own
//    params (assetload).
// The count is the guitar's for its whole life (InstrumentInfo +0Bh, copied from the profile at the build); nothing
// publishes or restores it at song setup any more (features/publish/publish.h Rs7Song_WidenDetector: only the per-song
// detector is widened).
namespace rs7clite {
// Desired count from the manifest keys. eight: [neck] eight (8 only with Rs7String7 next to Rs7String6).
unsigned Desired(bool has6, bool has7, bool eight);

// The profile's peg word (+3Ah): stock 0x19180006 / 0x19180004; count in the low byte.
constexpr uint32_t PegWord(unsigned n) {
    return 0x19180000u | (n & 0xFFu);
}

// Peg pool request count GKC writes (+3Ah low byte): PSQ1's 7 with the native PegSet, the count when larger.
unsigned PegCount(unsigned n, bool nativePegset);
// The string count of the guitar built for `player` (0/1) from the manager's profile: 7 or 8, else 6 (bass profile,
// no manager yet, unreadable). Guitar only.
unsigned BuildCountOf(uintptr_t mgr, unsigned player);
// Review F4 (1.0 scope): only player 0's guitar is ever built with 7/8 strings. GKC writes this count for the profile
// it loads: `desired` (6/7/8) for player 0, 6 for every other player (the desired count is global, set by player 0's
// song select, and player 1 cannot request a rebuild of its own). Anything unknown (player > 1) is 6 as well.
unsigned BuildCountFor(unsigned desired, unsigned player);
// Mode-entry normalisation of the second slot: true when `ret` is a mode entry and slot 1's guitar is built with 7/8
// (it cannot be after BuildCountFor, but a rebuild that bypassed GKM must not survive into a non-song mode).
bool NormaliseSlot1(uint32_t ret, unsigned slot1Count);
// Return addresses of 0x5B6030 whose tuning store write may trigger a rebuild (song select only).
bool AllowedCaller(uint32_t ret);
// Door (b), fix/clite3: a guitar built with 7/8 strings stays built until the next rebuild, so after a 7-string
// song every non-song mode would run with info+0Bh = 7 (Session Mode's harmony scoring 0x4435F0 / 0x444C30 /
// 0x445740 indexes [6][26] arrays with it, CONFIRMED static). The game stores a target tuning (0x5B6030) when it
// enters each of those modes, before the mode's own code runs; these are the return addresses of those stores
// (CONFIRMED static, docs/ARCHITECTURE.md). GKT rebuilds the guitar back to 6 there, through the same
// EnsureForPlayer path. Not listed on purpose: the tuner's in-session re-stores (0x7400B3 bass-emulation switch,
// 0x742B24 / 0x742C09 tuner event) and the song options path 0x69A7AF: they can run around a 7-string song, and a
// rebuild there would pull the guitar out from under it. The tuner menu stores 0x745E46 / 0x745EC3 have their own rule
// (kRetTunerMenu below, fix/menutuner).
//
// fix/escrebuild: 0x74236F is NOT a mode entry (it was listed as "Guitarcade tuner" in fix/clite3). Its function
// 0x742270 re-stores the profile's current tuning (Play_Tuning_GetProfileCurrentTuning, CapoFret / CentOffset) and
// its only caller is 0x73FF4F in the tuner's cancel handler 0x73FC80 ("OnCancel"; reached from the tuner menu's input
// event 5Bh in 0x73ED40 and from UI_TunerMenu_Tick 0x73E5B5), CONFIRMED static. Esc in a 7-string pre-song tuner
// went through it while the tuner was still alive: the rebuild freed the guitar the tuner's per-frame
// UI_TunerManager_Update 0x5B5850 -> 0x5B0660 -> Rnd_Guitar_ApplyTransforms 0x593020 then used (hs33 crash .mdmp:
// AV at Entity_SetTranslation 0x7ADF7E reading [0+D8h], fret bar entity of the freed guitar 1A567F30).
constexpr uint32_t kRetModeEntries[] = {
    0x60726C, 0x60C7D5,  // Guitarcade game entries (0x606F10, 0x60C580)
    0x613928,            // Lessons / guided experience (0x613700, GE_Song_LessonSongId, PreGame_GETuner)
    0x6DBD09, 0x6DBD63,  // Session Mode via its pre-session tuner (0x6DBBF0, SessionMode_PreSMTuner[MP])
    0x6F44BE, 0x6F44FA,  // Session Mode StartSession (0x6F4390, both before the harmony setup call 0x6F471A)
    0x73D904,            // Technique hub / first encounter (0x73D7D0, GE_TechniqueHub)
};
// The mode a return address of 0x5B6030 enters (log text), nullptr if it is not a mode entry.
const char* ModeEntry(uint32_t ret);
// What GKT does for a stored target tuning: the count to rebuild the guitar with, 0 = no rebuild.
//   song select (AllowedCaller), player 0: want, if it differs from the built count was;
//   a mode entry (ModeEntry), player 0: 6, if the guitar is built with 7/8 and the tuning has no Rs7 key (want 6);
//   anything else: 0.
//   tunerLive (any of them): 0 - a live player tuner holds the guitar (TunersHolding), a rebuild would free it under
//   the tuner. The next store from a listed caller decides again (the built count is unchanged).
unsigned RebuildTarget(uint32_t ret, uint32_t player, unsigned want, unsigned was, bool tunerLive = false,
                       bool songLive = false);
// fix/menutuner (b): the tuning-list screen "TuningOptionsMenu" (factory 0x5D48F0 registered under that name at
// 0x5D198E, vtable 0x11C2418, CONFIRMED static) stores the picked tuning in its select handler 0x745D90 and only then
// opens the "Tuner" / "TunerMP" screen (0x5D1E90 after 0x4079D0 "Tuner"): 0x745E46 = the store of player 0, 0x745EC3
// = the second store for multiplayer ("TunerMP" branch). The owner's crash (tests/owner-crash1) came through 0x745E46
// after a 7-string song: no rebuild there, the new tuner session ran on the 7-built guitar. GKT now rebuilds player 0's
// guitar to the picked tuning's count there (6, or 7/8 for a tuning with the Rs7 keys) like song select does - never
// while a player tuner holds a guitar (tunerLive) and never while a song is loaded (songLive: an SNG registered in
// features/nslot; HYPOTHESIS that the menu is not reachable from a song's pause menu - its navigation lives in the
// packed UI assets - so the song guard keeps a rebuild from pulling the guitar out from under a paused song).
constexpr uint32_t kRetTunerMenu[] = {0x745E46, 0x745EC3};
bool TunerMenu(uint32_t ret);
// The return address of 0x742270's store (tuner cancel, see above): not a mode entry, no rebuild.
constexpr uint32_t kRetTunerCancel = 0x74236F;
// Tuner manager (CONFIRMED static: global 0x135F5D4, vtable 0x11ACB10, cleared by its destructor 0x5B57C0;
// UI_TunerManager_Update 0x5B5850 walks 4 player tuners at +10h; a tuner with +10h != 0 is updated every frame and
// 0x5B0660 runs Rnd_Guitar_ApplyTransforms on its Rnd_Guitar* at +2Ch; one with +10h == 0 is deleted there).
constexpr uint32_t kTunerMgrGlobal = 0x135F5D4, kTunerMgrSlots = 0x10, kTunerGuitar = 0x2C;
constexpr unsigned kTunerSlotCount = 4;
// Number of player tuners of the manager `mgr` (0 = none) that hold a guitar (+2Ch != 0); SEH-guarded reads (an
// unreadable manager counts as one: no rebuild). The first such guitar goes to *guitar when given.
unsigned TunersHolding(uintptr_t mgr, uintptr_t* guitar = nullptr);

// Rnd_Headstock_LoadStringMeshes' loop count for an InstrumentInfo count n (GKH1/GKH2): at most the six slots.
constexpr unsigned HeadstockLoopCount(unsigned n) {
    return (n & 0xFFu) > 6u ? 6u : (n & 0xFFu);
}

constexpr uint32_t kGKH1Resume = 0x58E1BF, kGKH2Resume = 0x58E373;
constexpr uint32_t kSiteGKC = 0x593CC0, kGKCResume = 0x593CCB;
constexpr uint32_t kGKENotEqual = 0x596558;
constexpr uint32_t kEnsureForPlayer = 0x596030, kGuitarAttach = 0x4B24F0;
constexpr uint32_t kRetSetFromManifest = 0x697A24;  // after `call 0x5B6030` at 0x697A1F
// Guitar manager: +0Ch profile loader, +10h/+14h the per-player guitar holder (0x20 bytes, 0x4B2110):
// holder +04h Rnd_Guitar, +08h its profile, +10h load state (+28h == 3: ready).
constexpr uint32_t kMgrSlots = 0x10, kHolderGuitar = 0x04, kHolderProfile = 0x08, kHolderState = 0x10,
                   kStateReady = 0x28, kProfileCount = 0x38, kProfileType = 0x18, kGuitarInfo = 0x24;
}  // namespace rs7clite

// GKC GKE GKM GKL GKT GKH1 GKH2 (registration.cpp), group Clite.
struct Rs7HookTable;
extern const Rs7HookTable kRs7CliteHooks;

extern "C" {
__declspec(dllexport) extern uintptr_t g_rs7_clite;          // runtime switch (C-lite, group failure clears it)
__declspec(dllexport) extern uintptr_t g_rs7_clite_desired;  // count GKC writes into the next guitar profile
__declspec(dllexport) extern uintptr_t g_rs7_clite_mgr;      // guitar manager seen by GKM
}
// Starts the rebuild-timing watcher (log only). Call after the GK* sites are written.
void Rs7Clite_Start(bool eight);
// Startup, after the native ports (Rs7Publish_Start): C-lite is on when requested (the native PegSet and tuner are
// on) and the native ports are ready (g_rs7_clite), logged. Before any site is written.
void Rs7Clite_Configure(bool requested);
