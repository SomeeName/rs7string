# Stub layer refactor plan: `src/hooks/stubs.cpp`

Status: plan, not applied. 1.0 removed the legacy 7-string paths (C1-C5, N11, GSCL, the B-phase tuner T1 ... TD, the
stage-2 peg PG1-PG4 / PH1 / PH2, the S7 object N2-N4 / N6 / N7 / N10 / N14-N17, R7b / N9): their rows are gone from
the site tables below, the per-file counts of section 4 still include them. `stubs.cpp` is otherwise untouched; the mid-hook conversion (`midhook*`, `mid_*`, ARCHITECTURE section 15) has
landed since this plan was written, so recount the stubs below before applying it. Everything else of the stub layer
(`stubs_gs`, `stubs_nslot`, `stubs_pitch`, `stubs_publish*`, `stubs_chordloops`, `stubs_nextgrid`, `stubs_diagrams`,
`stubs_tuner8`, `stubs_fretgrid`, `stubs_psarc`) already follows the conventions below.

Evidence tags: **CONFIRMED** = read from the code or checked mechanically (the address, the displaced bytes of a site, the registry
flags, the relocator whitelist of `midhook.cpp` at the commit this branch started from). **HYPOTHESIS** = my reading of the role of a
game function; all hot / cold labels are HYPOTHESIS (derived from what the hooked function does, not profiled). A mid-hook verdict of
`Y` is CONFIRMED for the displaced bytes and HYPOTHESIS for the logic (no callback was written or run).

## 1. Glossary of site id prefixes

`sites.json` holds 731 sites. An id is a short label chosen when the site was found; the first letters name the area. The same
letters are sometimes reused for unrelated areas (marked "not"). The table partitions all sites (each site counted once).

| Prefix | Sites | Area | Meaning |
|---|---:|---|---|
| X_* | 72 | core | Data / function references bound at startup (`ref` / `callref` sites, never patched): X_CM colour manager, X_DT frame global, X_ALLOC allocator, ... |
| PP_* | 3 | psarc | Plain (unencrypted) PSARC: PP_TOC TOC decrypt block, PP_SNGF SNG flags check, PP_SNGD SNG decrypt call. Not the same family as PP1. |
| GK* | 7 | clite | C-lite: the guitar is built with the string count. GKC profile load, GKE profile Equals, GKH1/2 headstock mesh loop clamp, GKL Lua GetGuitarStringCount, GKM guitar-manager memo, GKT target-tuning hook. All mid-hooks. |
| LS* | 4 | diagnostics | Rnd_Guitar_LoadStep probes (LSA LSC LSE LSG), mid-hooks, [debug] publish_trace=1 only. |
| CM<addr>, CM* | 167 | native colours | Native 8-column colour manager (`*0x135F58C`): CM<hex address> are in-place byte patches of colour-state offsets (162), CMUV CMIX CMINIT CMBUILD CMFALL native colour publication stubs. |
| FGR<addr>, FGRB | 71 | native fretgrid | Native capacity-8 FretGrid: FGR<hex address> in-place layout patches (cell stride 1CCh -> 264h, 70), FGRB the Rnd_FretGrid_Build exit stub. Generated (tools/gen_fretgrid.py). |
| FGCT, FGHK | 2 | native fretgrid | Generic mid-hooks: FGCT registers grids built after the layout was written, FGHK writes the deferred FGR sites on the game thread. |
| NGB<addr>, NG1 NGP NG9 | 28 | native nextgrid | Native 8-row next-note grid: NGB<hex address> in-place patches (25, generated), NG1 cell index, NGP stack probe, NG9 neck targets / tree release. |
| PUB* | 25 | publish | Publication of the real string count (7/8) in InstrumentInfo +0Bh: PUBG* grid ports, PUBCL / PUBTO / PUBWU preparation-function copies, PUBNL / PUBAN / PUBAC pitch readers, PUBNN* capacity-8 variants of NN*, PUBZ0-6 standard-zero pitch wrappers, PUBEDGE edge string. |
| CL* | 19 | chord loops | [chords] native_chordloops: eight-slot chord bends / sustain techniques (CLB* bend, CLBA bend data, CLGU / CLGE chord ghost bend, CLVB stop-pluck, CLNF / CLCT loop bounds). |
| DG*, TP13 | 4 | diagrams | Chord-diagram data: DGB binds the attribute dictionary, DGM / DGS add the LowB / LowFs keys, TP13 template lookup. |
| GS<xx> entries | 16 | native strings | Entry dispatchers of the 16 copied GuitarStrings functions (Free, Bind, Unload, Load, FadeOut, FadeIn, UpdateTransforms, entity pass, Update, ResetState, Pluck, vibration, InitColors, section dimming, glow, headstock segment colours). |
| GSA GSC GSZ GIA GIZ GRM GSSS GSSE | 10 | native strings | Native GuitarStrings owners: GSA1 / GSC1 / GSZ1 alloc / ctor / free of the 0xB0 object, GIA1 / GIZ1 InstrumentInfo 0x48, GRM1-3 renderer mesh collection, GSSS / GSSE song start / end dimming flag. |
| PS* | 16 | native pegset | Native 8-slot PegSet: PSA alloc size, PSC ctor tail, PSD destructor, PSL load, PSM renderer mesh collection, PSN named colours, PSQ peg pool request, PSR colour reset, PSU unload. |
| LT1, AL1 | 2 | pegs / headstock | LT1 in-line flag for the tuner letters, AL1 nif handler entry (edited headstock model). The legacy tuner letters TLC / TLI / TLO1 / TLO2 went in 1.0. |
| TU8*, TS8* | 37 | native tuner | Native 8-slot tuner session: TU8ALLOC / TU8INIT / TU8FREE session storage, TU8H / TU8C / TU8A GuitarStrings reader entries, TS8* the 21 UI_PlayerTuner method copies (+ the mid-hooks TS8*, tools/gen_tuner8s.py). |
| TK TS W AT TF1 | 5 | tuner | Tuner UI hooks: TK manifest key, TS ApplyTuning (hands over to the native session), W wrong-instrument threshold, AT autotune (debug), TF1 camera pose. TS is not TS8. The B-phase hooks (T1 TA TR TF TD L1 L2 M1 M2 PK PE) went in 1.0. |
| TJ TU TUT TN | 4 | tuning names | Seven-string tuning names: TJ manifest resolver, TU / TUT arrangement-UUID resolver, TN tuner array resolver. TU is not TU8*. |
| PA PM PR0 PT PE0 PC | 11 | native pitch | Native 8-entry tuning / open-MIDI storage: PA detector / matcher allocation, PM matcher tail, PR0 detector reset, PT1 detector bass exit, PE0 expected-pitch port (0x873E90), PC1-4 callers of 0x873F80. PR0 is not PR1-3. |
| S*, I1 | 5 | pitch | Detection basics: S1 / S2 expected MIDI for string 6 (legacy), S3 detector tuning exit, S4 song-setup tuning call (mode signal), I1 inverse pitch (MIDI to fret). |
| R* | 7 | neck | Lane geometry of string 6 (stock layout): R1 GetCellPos entry (now PUBGP), R2 note head z, R3a/b/c NoteFx position, R4 feedback position, R6 lane offset y (R7b went in 1.0). |
| D* | 3 | diagnostics | Diagnostics only ([debug] diagnostics): D2 / D3 mid-hook examples, D5 gem position log. |
| N<n> | 11 | strings | The seventh string line's neighbours: N1 / N21 pool request counts, N5 per-guitar update, N12 / N13 section string mask, N18-N23 bends on the native string 6 / its ghost. The stage-1 S7 object sites (N2-N4, N6, N7, N10, N11, N14-N17) went in 1.0. |
| F1, IN1, V2, BD* | 7 | neck | Neck geometry: F1 fret bars, IN1 inlays, V2 slide offset stride carrier, BD1-4 bend direction perStringMode guards. |
| B1 B2 | 2 | chords | Chord frame boxes (M4): B1 marks the geometry, B2 scales marked frames along the string axis. |
| TP* CP* PP1 SF1 | 14 | chords (N-slot data) | T8 template / C8 ChordNotes record redirects (TP*, CP*), song load / free of the arrays (PP1, SF1). |
| XB1 | 1 | song | RS7X extension block read at the end of SNG_Song_Load. |
| N-slot render bytes | 67 | chords (N-slot render) | In-place byte patches of the game chord code over the 8-slot records: NR chord render object, NB chord box, NS spawner, CT sustains, SH slide-to heads, BH bend head, HS handshape, NN next-note register, NK neck target, NM neck-target motion, FG finger display, SM section string mask, DM section dimming template. |
| N-slot detection / tracking | 65 | chords (N-slot detect) | TM string-mask test, DS double stop, MD matcher entry, RQ / PR1-3 / MS detection request, LU Lua binding, AM mistake analysis, WC whole-chord update, EV eval wrappers, BT / CS eval tails, PL pluck, CN census, CC chord string to note, BF bend copy, CH child evals, RL release, AG aggregate, UP / PD technique latches, NF note-in-progress chord loop. |
| SE* | 3 | song | Song-end log: SE1 end-song event, SE2 leave-state call, SE3 NULL song object in state 3. |
| TICK JUD* AUD1 | 4 | virtual guitarist | Test mode ([test] vguitar): TICK note list, JUD1 / JUD2 judgement, AUD1 input injection. |

Id collisions to know: `PE` (tuner pick epilogue) vs `PE0` (expected pitch port); `PR0` (detector reset) vs `PR1`-`PR3` (N-slot detection);
`TU` (UUID tuning name) vs `TU8*` (native tuner); `TS` (ApplyTuning) vs `TS8*`; `PP1` (N-slot song arrays) vs `PP_*` (plain PSARC);
`FG1`-`FG9` (N-slot finger display) vs `FGR*` (FretGrid layout); `CM*` (colour manager) vs `CL*` (chord loops);
`N1`-`N23` (stage-1 string line) vs `NG*` / `NN*` / `NK*` / ... (next-note grid, chord render).

Sites are written by kind: `jmp` (a stub or mid-hook), `call` / `callref` / `rel32` (a call or branch retargeted), `bytes` (in-place byte
patch, no code), `ref` (a bound game address). Only the `jmp` / `call` / `rel32` sites below need code in `stubs*.cpp`.

## 2. Conventions already in place (`src/hooks/stub_macros.h`)

When `stubs.cpp` is split, use these instead of the local copies (the definitions are token-identical to the ones in `stubs.cpp`, so
a file may include the header while the old definitions still exist):

- `RS7_VAR`, `RS7_STUB`, `RS7_RESUME_VAR(id)`, `RS7_TARGET_VAR(id)`: exported link variables and naked functions.
- `RS7_SAVE_ALL` / `RS7_RESTORE_ALL`: full state save (flags, GPRs, x87/SSE); identical to `RS7_VG_ENTER` / `RS7_VG_LEAVE` in `stubs.cpp`.
- `RS7_ENTRY_DISPATCH*`: copy-or-original entry dispatch (not needed by `stubs.cpp`, which has no copied functions).
- `RS7_BOUND_GUARD`, `RS7_GS_IF_NATIVE`: guard shapes of the publication ports / native GuitarStrings.

Verification of every step is a byte comparison against a build of the previous commit (see section 6); moving a stub between
files changes link order, so after a move use the per-function comparison of `tools/cmp_stub_bytes.py`.

## 3. Stubs in `stubs.cpp`

103 site stubs and 3 helpers. Columns:

- **Address**: the first address in the site note of `sites.json` (`tools/sitecheck.py` verifies the byte pattern of every site).
- **Hot**: `H` = runs per frame / per note / per gem / per audio buffer on the song path (HYPOTHESIS), `C` = cold (song or guitar load,
  tuner screen, one-time, logging, tests).
- **Mid-hook**: can the generic mid-hook (`void __cdecl Callback(Rs7Mid::Context&)`, `midhook.h`) replace the hand-written stub? `Y` = yes.
  `Y*` = yes with a trick (`ret` emulation through `ctx.esp` / `ctx.jump`). `N` = no, with the reason. The first reason class is
  mechanical (CONFIRMED by running the relocator whitelist of `midhook.cpp` over the `expect` bytes of the site: prefix `0x66`, `FE`-group
  `inc`/`dec`, `imul`, x87 other than `D9` memory forms, `ret imm`, a `ret` inside the span); the second is structural (post-call code,
  wrapping a whole function, an x87 result that must stay in `ST0`).
- **Group**: duplicate-shape group (section 5).

| ID | Address | Purpose | Target file | Hot | Mid-hook | Group |
|---|---|---|---|:-:|---|:-:|
| S1 | 0x873F98 | legacy: guitar string >5 base pitch (branch target of the JA in 0x873F80), Rs7_Base6 | stubs_pitch.cpp | H | N: rel32 retarget of a branch, not a jmp site | CB |
| S2 | 0x873E9F | legacy: single-note expected MIDI for string 6 (35 + t6, Rs7_NoteString6); not reached with native_pitch (PE0) | stubs_pitch.cpp | H | Y | CB |
| I1 | 0x874056 | inverse pitch feedback (MIDI to fret) for string 6 of a 7-string song | stubs_pitch.cpp | C | Y*: ends in pop ebp / ret (ret emulation) | RP |
| S3 | 0x4E263B | detector tuning exit: copies the tuning, Rs7_OnDetectorTuned | stubs_pitch.cpp | C | N: 0x66 prefix in the displaced mov word, site covers the ret | RP |
| S4 | 0x7DF0FC | song-setup call of Play_Detector_SetTuning: Rs7_OnSongTuning (mode signal) before it | stubs_pitch.cpp | C | Y: callback before the relocated call | CR |
| R2 | 0x79E726 | note head lane z for string 6 = 2*cell0.z - cell1.z (+ D1 diagnostics) | stubs_neck.cpp | H | N: x87 result of the replayed fld from a computed address | LN |
| R3a | 0x40CDA1 | NoteFx position (open notes) for string 6: scratch vec3 = 2*cell0 - cell1 | stubs_neck.cpp | H | N: x87 fld, result address in a live register | LN |
| R3b | 0x40CDE0 | NoteFx position (other notes) for string 6: scratch vec3 = 2*cell0 - cell1 | stubs_neck.cpp | H | N: x87 fld, result address in a live register | LN |
| R3c | 0x40CE1E | NoteFx position (fretted notes) for string 6: scratch vec3 = 2*cell0 - cell1 | stubs_neck.cpp | H | N: x87 fld, result address in a live register | LN |
| R4 | 0x7A701E | note feedback position for string 6 (scratch vec3) | stubs_neck.cpp | H | N: x87 fld, result address in a live register | LN |
| R6 | 0x7A8C2A | lane offset y (open-note decoration, HYPOTHESIS) for string 6 | stubs_neck.cpp | H | N: x87 fld, result address in a live register | LN |
| N1 | 0x5965CA | type-4 string-line pool request count 6 -> 7 (8 with the publication ports) | stubs_strings.cpp | C | Y | PC |
| N5 | 0x592AD4 | Rnd_Guitar_Update: per-guitar 7-string state and the native strings / frets / pegs frame (Rs7_S7Update) | stubs_strings.cpp | H | Y | CR |
| N12 | 0x55CC71 | section string mask: string 6 notes set bit 6 (single-note branch) | stubs_strings.cpp | C | Y: branch decision via ctx.jump | PC |
| N13 | 0x55CE3A | section string mask back-fill: same for bit 6 after string 5 | stubs_strings.cpp | C | Y | CB |
| N18 | 0x7E3FFA | single-note bend on the 7th string (bendup_NN) | stubs_strings.cpp | H | Y: displaced cmp + jae, ctx.jump to the join address | SB |
| N19 | 0x7E4059 | bend release on the 7th string while sounding | stubs_strings.cpp | H | Y: displaced cmp + jae, ctx.jump to the join address | SB |
| N20 | 0x7E43DB | bend release when a bend note ends | stubs_strings.cpp | C | Y: displaced cmp + jae, ctx.jump to the join address | SB |
| N21 | 0x5965E9 | type-2 ghost-string pool request count 6 -> 7 (8 with the publication ports) | stubs_strings.cpp | C | Y | PC |
| N22 | 0x7E3ED5 | chart bend on the 7th string ghost (ghost_up_NN) | stubs_strings.cpp | H | Y: displaced cmp + jae, ctx.jump to the join address | SB |
| N23 | 0x7E4347 | ghost bend release when a note ends | stubs_strings.cpp | C | Y: displaced cmp + jae, ctx.jump to the join address | SB |
| F1 | 0x58FB20 | fret bars of a 7/8-string guitar: stock body as subroutine, then Rs7_FretsAfterApply | stubs_neck.cpp | H | N: wraps the whole function (post-return hook) | WR |
| IN1 | 0x593074 | inlay dots of a 7/8-string guitar: after the stock Rnd_Inlays_ApplyTransforms (call site) | stubs_neck.cpp | H | N: post-call hook | WR |
| TF1 | 0x5B26B5 | tuner camera pose: reference string min(count, 6) - 1 | stubs_tuner.cpp | C | N: displaced dec bl (FE opcode) not whitelisted | PC |
| LT1 | 0x5B1B53 | tuner letters: in-line flag +0Ah for a 7-string in-line headstock on a 3+3 frame | stubs_tuner.cpp | C | Y | CR |
| PSA2 | 0x58DA05 | native PegSet: sized free 15Ch -> 1D4h | stubs_pegs.cpp | C | Y: own push instead of the replayed immediate | PD |
| PSD1 | 0x59C30D | native PegSet destructor: release counter 5 -> 7 | stubs_pegs.cpp | C | Y | PD |
| PSD2 | 0x59C314 | native PegSet destructor: release anchor +104h -> +154h | stubs_pegs.cpp | C | Y | PD |
| PSU1 | 0x59C3C2 | native PegSet unload: give-back loop entry bound 8 | stubs_pegs.cpp | C | N: 0x66 prefix in the displaced cmp cx,[edi+4] | PD |
| PSU2 | 0x59C414 | native PegSet unload: loop latch bound 8 | stubs_pegs.cpp | C | N: 0x66 prefix in the displaced cmp bx,[edi+4] | PD |
| PSU3 | 0x59C41D | native PegSet unload: taken-flag clear 6 -> 8 slots | stubs_pegs.cpp | C | Y | PD |
| PSM1 | 0x541B99 | native PegSet, renderer setup: peg mesh slot bound 8 | stubs_pegs.cpp | C | N: 0x66 prefix in the displaced cmp cx,[eax+4] | PD |
| PSM2 | 0x541D34 | native PegSet, renderer setup: pass latch bound 8 | stubs_pegs.cpp | C | Y | PD |
| PSC1 | 0x59C2F4 | native PegSet ctor tail: slots 6/7, pegColor[8], rs7 tail (Rs7_PegSetCtor) | stubs_pegs.cpp | C | Y | CR |
| PSL1 | 0x59C4D0 | native PegSet load: a 6-string guitar loads 7 slots | stubs_pegs.cpp | C | Y | PD |
| PSL2 | 0x59C60C | native PegSet load complete: stock call, then Rs7_PegSetLoaded (call site) | stubs_pegs.cpp | C | N: post-call hook | WR |
| PSR1 | 0x5B0A21 | tuner peg colour reset: loop bound = the PegSet active count | stubs_pegs.cpp | C | Y | PD |
| PSN1 | 0x5B089D | tuner SetPegColorAnim: colour name for peg index 6/7 | stubs_pegs.cpp | C | Y | PD |
| PSN2 | 0x5B08F6 | tuner SetPegColorAnim: named-colour dispatch, 7th colour after it (call site) | stubs_pegs.cpp | C | N: post-call hook + NULL-slot skip | WR |
| V2 | 0x7A69BE | slide offset: end-cell address with the native FretGrid row stride g_rs7_fg_row | stubs_neck.cpp | H | N: imul in the displaced bytes (operand patch) | LN |
| BD1 | 0x79FE9B | bend direction: string 6 reads perStringMode[0] (guard cmp string,6) | stubs_neck.cpp | H | Y: ctx.jump to the computed continuation | BG |
| BD2 | 0x7A663B | bend direction: string 6 reads perStringMode[0] (guard cmp string,6) | stubs_neck.cpp | H | Y: ctx.jump to the computed continuation | BG |
| BD3 | 0x7A6D80 | bend direction: string 6 reads perStringMode[0] (guard cmp string,6) | stubs_neck.cpp | H | Y: ctx.jump to the computed continuation | BG |
| BD4 | 0x7A764D | bend direction: string 6 reads perStringMode[0] (guard cmp string,6) | stubs_neck.cpp | H | Y: ctx.jump to the computed continuation | BG |
| D5 | 0x7A4E5C | diagnostics: log gem positions of strings 0 and 6 ([debug] diagnostics only) | stubs_neck.cpp | H | Y | CR |
| TK | 0x5B8480 | tuning offsets reader entry: read the manifest key Rs7String6 (Rs7_TunerKey) | stubs_tuner.cpp | C | Y | CR |
| TS | 0x5AF200 | Play_TunerSession_ApplyTuning entry: hand a 7/8-string target to the native session (Rs7_TunerApply) | stubs_tuner.cpp | C | Y | CR |
| W | 0x5B2DDE | wrong-instrument check: fcomp threshold from the session ([tuner] lowrange) | stubs_tuner.cpp | C | N: x87 operand replacement (displaced fcomp) | PC |
| AT | 0x5B0C20 | IsStringTuned entry: auto-tune N seconds after the selection ([debug] tuner_autotune only) | stubs_tuner.cpp | C | Y*: early ret 0Ch (ret emulation) | CB |
| AL1 | 0x4A8560 | Res_NifMemHandler entry: [assets] headstock7 merges the edited model (tail jump into Rs7_NifHandler) | stubs_pegs.cpp | C | N: function detour that must call the stock body (Rs7_NifHandlerStock) | WR |
| TICK | 0x7E2880 | vguitar: Play_Session_UpdateNoteDetection entry, live notes to the synth schedule | stubs_vguit.cpp | H | Y | FS |
| JUD1 | 0x7E2CFF | vguitar: note window closed, per-note report | stubs_vguit.cpp | H | Y | FS |
| JUD2 | 0x7E6AD0 | vguitar: Play_Session_ScoreNote entry, scored hit/miss | stubs_vguit.cpp | H | Y | FS |
| AUD1 | 0xC4618C | vguitar: SndIn_PA_Read, synthesized samples into the input buffer (audio thread) | stubs_vguit.cpp | H | Y | FS |
| B1 | 0x7A2BCA | chord frames: Rs7Chord_OnBoxGeom marks frames of 7/8-string chords after the GemGeom ctor | stubs_chords.cpp | H | Y | CR |
| B2 | 0x7A91E4 | chord frames: Rnd_GemGeom_ApplySpan scales marked frames along the string axis (call site) | stubs_chords.cpp | H | N: replaces the callee (conditional extra calls, then ret) | WR |
| TJ | 0x56C950 | manifest tuning-name resolver: 7-string name (pre / stock call / post) | stubs_tuner.cpp | C | N: wraps the function (pre/post, own stack frame) | WR |
| TU | 0x56C300 | arrangement-UUID tuning-name resolver: 7-string name (pre / stock call / post) | stubs_tuner.cpp | C | N: wraps the function | WR |
| TUT | 0x56C408 | records the arrangement Tuning object found inside the UUID resolver | stubs_tuner.cpp | C | Y | CR |
| TN | 0x5B7750 | tuner array tuning-name resolver: 7-string name (pre / post, may skip the original) | stubs_tuner.cpp | C | N: wraps the function | WR |
| SE1 | 0x7DD600 | song-end log: Play_Session_OnEndSongEvent entry | stubs_song.cpp | C | Y | FS |
| SE2 | 0x7DFA30 | song-end log: leave-state call entry | stubs_song.cpp | C | Y | FS |
| SE3 | 0x7E0AE6 | song-end log: state 3 with a NULL song object | stubs_song.cpp | C | Y | FS |
| XB1 | 0x55922C | RS7X extension block read at the end of SNG_Song_Load | stubs_song.cpp | C | Y: callback before the relocated call | FS |

Helpers (not sites):

| Name | Purpose | Target file |
|---|---|---|
| Rs7_PegOwnedZF | helper: ZF = 1 iff the PegSet is in g_rs7_pegsets (native PegSet test) | stubs_pegs.cpp |
| Rs7_PegDisownZF | helper: as Rs7_PegOwnedZF, a found entry is cleared (PSA2 only) | stubs_pegs.cpp |
| Rs7_NifHandlerStock | helper: the stock nif handler for Rs7_NifHandler (replay of AL1 + jump back) | stubs_pegs.cpp |

## 4. Target files and counts

| File | Stubs | Hot | Cold | Mid-hook candidates (Y / Y*) | Legacy-only (written with native_*=0) | Notes |
|---|---:|---:|---:|---:|---|---|
| stubs_pitch.cpp | 5 | 2 | 3 | 3 | - | extends the existing file (native pitch ports); S2 is bypassed when native_pitch replaces the entry (PE0) |
| stubs_neck.cpp | 14 | 14 | 0 | 6 | - | lane geometry, bend direction guards, fret bars, inlays; hot x87 code |
| stubs_strings.cpp | 20 | 10 | 10 | 18 | N2, N3, N4, N6, N7, N14, N15, N16, N17 | stage-1 seventh string line (N*) and its bends; mostly legacy S7 and a few always-on guards |
| stubs_tuner.cpp | 21 | 0 | 21 | 13 | - | tuner UI hooks and tuning names (could split `stubs_tuningnames.cpp`: TJ TU TUT TN) |
| stubs_pegs.cpp | 25 | 3 | 22 | 13 | PG1, PG2, PG3, PG4, PH2, PH1 | legacy peg mirror, native PegSet, peg tint, tuner letters / fades of the 7th peg, headstock model entry |
| stubs_vguit.cpp | 4 | 4 | 0 | 4 | - | extra file: the test-mode virtual guitarist (written only with [test] vguitar=1) |
| stubs_chords.cpp | 2 | 2 | 0 | 1 | - | chord frame boxes (the N-slot chord stubs already live in stubs_nslot.cpp) |
| stubs_song.cpp | 4 | 0 | 4 | 4 | - | RS7X block, song-end log |
| **total** | 103 | 41 | 62 | 68 (of which Y*: 5) | 23 legacy | |

Not mid-hook material (35), by reason:

- rel32 retarget of a branch, not a jmp site: S1
- 0x66 prefix in the displaced mov word, site covers the ret: S3
- x87 result of the replayed fld from a computed address: R2
- x87 fld, result address in a live register: R3a R3b R3c R4 R6
- wraps the whole function (post-return hook): F1
- post-call hook: IN1 PSL2 TLC TLO1 TLO2 TF
- displaced dec bl (FE opcode) not whitelisted: TF1
- 0x66 prefix in the displaced cmp cx,[edi+4]: PSU1
- 0x66 prefix in the displaced cmp bx,[edi+4]: PSU2
- 0x66 prefix in the displaced cmp cx,[eax+4]: PSM1
- post-call hook + NULL-slot skip: PSN2
- pre and post the call: TLI
- imul in the displaced bytes (operand patch): V2
- x87 operand replacement (displaced fcomp): W
- ret inside the site span: TA
- ret 1Ch in the displaced bytes: PE
- function detour that must call the stock body (Rs7_NifHandlerStock): AL1
- wraps the whole function: PH2
- 0x66 prefix in the displaced cmp dx,[edi+4]: PH1
- replaces the callee (conditional extra calls, then ret): B2
- wraps the function (pre/post, own stack frame): TJ
- wraps the function: TU TN

The `0x66`-prefix compares (S3 PSU1 PSU2 PSM1 PH1) and the `inc` / `dec r8` forms (TF1) would become relocatable if the relocator grew
those two forms; that is a decision for the mid-hook work, not for this plan.

Hot-path sites with a mid-hook verdict Y / Y* (25): S2 N5 N18 N19 N22 BD1 BD2 BD3 BD4 D5 TICK JUD1 JUD2 AUD1 B1.
They are candidates only after the cost is measured: ARCHITECTURE.md gives about 150 ns per call for the thunk (one machine,
microbenchmark); the per-gem and per-frame sites run many times per frame. Keep those as stubs until a frame-time measurement says otherwise.

## 5. Duplicate-shape groups

| Group | Name | Shape | Proposed handling | Members |
|:-:|---|---|---|---|
| CR | call + replay | `[flag check]; pushad; push args; call C++; add esp; popad; replay displaced; jmp resume` - the C++ side only observes or mutates memory | most of these become mid-hooks (see the mid-hook column); the rest one macro `RS7_CALL_REPLAY` | S4 N9 N2 N3 N5 N6 N7 N15 N16 N17 LT1 PG2 PG3 PG4 PSC1 D5 TK TS T1 TA TD PK PE B1 TUT |
| CB | call + branch | as call + replay, then test the result (or a compare) and continue at resume / target / another address | macro with a result label, or mid-hook with `ctx.jump` | S1 S2 N4 N10 N13 N14 TR AT M1 M2 |
| WR | wrap the stock function or call | run the original (call or subroutine), then C++ (or C++ before and after) | one wrapper macro (`RS7_WRAP_POST`); not mid-hook material (post-return code) | F1 IN1 PSL2 PSN2 TLC TLI TLO1 TLO2 TF AL1 PH2 B2 TJ TU TN |
| PC | predicate constant | if a predicate holds (native object, switch, string >= 6) replace one constant / register / operand, else replay the stock instruction | mid-hook callback writing the register, or a declarative table `site -> predicate -> immediate` | N1 N12 N21 TF1 PG1 W L1 L2 |
| PD | PegSet native-or-stock bound | call Rs7_PegOwnedZF, then pick the 8-slot or the 6-slot constant / bound (a subset of the predicate group) | one macro `RS7_IF_PEGSET_OWNED(reg, stock-asm, native-asm)` that contains the ownership call | PSA2 PSD1 PSD2 PSU1 PSU2 PSU3 PSM1 PSM2 PSL1 PSR1 PSN1 PH1 |
| LN | lane clone | `string 6 position = 2 * cell0 - cell1` into a scratch value, optional clone switch (R3a / R3b / R3c / R4 differ only in the scratch variable and the resume address) | one macro `RS7_LANE_CLONE(ID, scratch, tail)`; x87 and hot, so stays hand-written asm | R2 R3a R3b R3c R4 R6 R7b V2 |
| BG | perStringMode guard | `cmp string,6; jae`: string 6 reads perStringMode[0] and joins the stock body at a fixed offset (BD1-BD4 differ in the replayed body) | one macro `RS7_PSM_GUARD(ID, string-reg, replay, join-offset)` | BD1 BD2 BD3 BD4 |
| SB | S7 bend on string 6 | `cmp string,[info+0Bh]; jae`, Rs7_S7BendTarget(kind), join at resume + n (N18 N19 N20 N22 N23) | one macro `RS7_S7_BEND(ID, kind, ...)`; legacy S7 path (native_strings=0 only) | N18 N19 N20 N22 N23 |
| FS | full-save logging / test wrapper | `[switch]; RS7_VG_ENTER; push args; call; RS7_VG_LEAVE; replay` (TICK JUD1 JUD2 AUD1 SE1 SE2 SE3 XB1) | one macro on top of `RS7_SAVE_ALL` (identical to RS7_VG_ENTER / LEAVE) | TICK JUD1 JUD2 AUD1 SE1 SE2 SE3 XB1 |
| RP | function end / entry replacement | replaces a function tail or entry and returns itself (I1, S3) | keep hand-written | I1 S3 |

Macro candidates, in order of value (each is only worth it if the generated bytes stay identical, which the check below proves):

1. `FS` (full-save logging wrappers): 8 stubs of one shape on top of `RS7_SAVE_ALL`.
2. `PD` (PegSet owned-or-stock): 12 stubs sharing the `Rs7_PegOwnedZF` call and the 6/8 choice.
3. `SB` + `BG` (bend on string 6 / perStringMode guard): 5 + 4 stubs of one shape each.
4. `LN` (lane clone): R3a / R3b / R3c / R4 are the same text with a different scratch variable and resume (the x87 code stays asm).
5. `PC` / `CR`: only worth a macro where three or more share the exact instruction order; most are better as mid-hooks.

## 6. Application order and proof

1. Split by area, one file per commit, no code change in the same commit. Keep the definition order of `RS7_VAR` variables and stubs inside a
   moved block. Add the new files to `rs7string.vcxproj`.
2. After each commit: `build.ps1`, then compare against a build of the previous commit with `tools/cmp_stub_bytes.py OLD.dll NEW.dll`:
   section data identical (debug directory masked) = byte-identical image; otherwise every exported function is compared instruction by
   instruction with address-like operands masked. A pure move reports "same instructions, different layout".
3. `uv run pytest tools\tests -q` (4182 tests at the start of this work), `uv run python tools\sitecheck.py`,
   `uv run python tools\check_exports.py bin\Release\dinput8.dll` (827 exports must stay).
4. Export and variable names never change (`Rs7_Stub_<ID>`, `g_rs7_resume_<ID>`, `g_rs7_target_<ID>`): `registry.cpp`, `tools/check_exports.py`
   and `tools/tests/test_emu*.py` find them by name.
5. Mid-hook conversions are separate commits per stub with their own emulator comparison (AGENTS.md "Adding or removing a hook site"); they
   change the generated code on purpose and are not covered by the byte check.
