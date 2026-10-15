# rs7string architecture

**How the mod works, which decisions shaped it, and what is known to be open.**

[Contributing](../CONTRIBUTING.md) · [Development](DEVELOPMENT.md) · **Architecture** · [RS7X spec](release/RS7X-SPEC.md) · [Releasing](release/RELEASING.md) · [Agents](../AGENTS.md)

For build, test and tooling see [DEVELOPMENT.md](DEVELOPMENT.md); for players see the
[README](../README.md) and [docs/release](release/README.md).

> [!NOTE]
> **Tags.** **CONFIRMED** means checked against the game's code (address given), an emulator run or
> an in-game test. **HYPOTHESIS** means a plausible reading that has not been verified.

> [!IMPORTANT]
> **Addresses** are for the Steam Rocksmith 2014 Remastered executable of September 2022 (PE checksum
> `0x00B13D7C`, image base `0x400000`, 32-bit, VMProtect-packed on disk, unpacked in memory at run
> time). Function names such as `Rnd_FretGrid_GetCellPos` are our own labels from reverse
> engineering, not Ubisoft names. The mod ships no Ubisoft data and uses no Ubisoft keys.

Older notes, per-job reports and review logs from the development of 1.0 were removed from the tree
before the repository went public. They remain in the git history (last commit that holds them:
`26b7aa1`, paths `docs/*-report.md`, `docs/eight-*.md`, `research/`).

## Contents

| # | Section | # | Section |
|---|---|---|---|
| 1 | [What the mod does](#1-what-the-mod-does) | 8 | [Tuner and tuning names](#8-tuner-and-tuning-names) |
| 2 | [Patch mechanism](#2-patch-mechanism) | 9 | [Chord diagrams](#9-chord-diagrams-chords-diagrams7) |
| 3 | [The string-count model (C-lite)](#3-the-string-count-model-c-lite-neck-clite1) | 10 | [Plain (unencrypted) PSARC loading](#10-plain-unencrypted-psarc-loading) |
| 4 | [Native capacity-8 layouts](#4-native-capacity-8-layouts) | 11 | [Window, input, overlay, logging](#11-window-input-overlay-logging) |
| 5 | [Rendering the seventh string](#5-rendering-the-seventh-string) | 12 | [Virtual guitarist](#12-virtual-guitarist-test-mode-test-vguitar1) |
| 6 | [Detection and scoring](#6-detection-and-scoring) | 13 | [Eight strings](#13-eight-strings) |
| 7 | [Chords with the 7th string](#7-chords-with-the-7th-string-native-n-slot) | 14 | [Known limitations and open risks](#14-known-limitations-and-open-risks-10) |
| | | 15 | [Mid-hook contract and cold-site conversion](#15-mid-hook-contract-and-cold-site-conversion) |

## 1. What the mod does

A `dinput8.dll` proxy that patches the running game so a guitar can have 7 (experimentally 8)
strings. In a 7-string arrangement the SNG tuning has 7 entries, string index 6 is the low B, and its
expected MIDI note is `35 + tuning offset + fret`. Everything else (6-string guitar, bass, stock
songs, existing CDLC) runs the stock code.

## 2. Patch mechanism

**Startup.** The proxy forwards to the system `dinput8.dll`. A worker thread waits for `d3d9.dll`
(the exe is unpacked by then), checks the PE checksum, resolves every site, and writes jumps into
the DLL's stubs. The DLL itself imports only kernel32, user32 and bcrypt (`tools/check_exports.py`
enforces "no d3d9 import").

**Sites** are listed in `sites.json`; `tools/gen_sites.py` turns them into
`src/generated/sites.generated.h` (not committed). A site has an id, a byte `pattern` (with `??`
wildcards) that must match exactly once in `.text`, an `offset`, the `expect`ed stock bytes and a
kind: `jmp` (overwrite with a jump into a stub), `bytes` (in-place byte patch), `ref` (read the
dword at the site, e.g. a game global), `callref` (read the callee of the `E8` at the site).
Resolution is **all or nothing**: any missing or mismatching site patches nothing. Every site has a
`note` with its purpose and address. `tools/sitecheck.py` re-resolves all sites against an unpacked
copy of the game image and also checks that each site covers whole instructions and has no inbound
branch.

**Stubs** (`src/hooks/stubs*.cpp`) are naked functions that replay the displaced stock
instructions. They are checked against the stock code in the Unicorn emulator
(`tools/tests/test_emu*.py`).

**Mid-hooks** (`src/hooks/midhook.*`, CONFIRMED by unit and emulator tests) give new sites a plain
C++ callback `void __cdecl Callback(Rs7Mid::Context&)` instead of hand-written assembly. One common
naked thunk saves GPRs, EFLAGS and x87/SSE state, clears DF, calls C++, restores, and returns into a
relocated copy of the displaced instructions, which jumps back to `site + stolen length`.
`ctx.esp` is the original game ESP; registers and flags are mutable; assigning `ctx.jump` skips the
replay. The relocator whitelists common ALU/MOV/LEA/TEST, register PUSH/POP, immediates,
MOVZX/MOVSX/SETcc and relative CALL/JMP/Jcc; it rejects prefixes, LOOP/JCXZ, indirect transfers,
unknown opcodes, branches into instruction interiors, internal calls and call/pop. There is no
blind-copy fallback. Callees that inspect their return address need a hand-written hook (contract,
conversion method and status: section 15). Cost
measured on one machine: about 150 ns per call (microbenchmark, not frame time). Live detach is
not supported. D2/D3 in `mid_examples.cpp` show the registration pattern. To add one: add a `jmp`
site (5..16 bytes, whole instructions), implement the callback, set `Rs7Hook::callback`, add an
emulator comparison, run the gates.

**Copied game functions.** Some stock functions are copied byte for byte from the game image with
only array displacements and count bytes moved to the capacity-8 layouts (`tools/gen_fncopy.py`,
`gen_tuner8s.py`, `gen_publish*.py` -> `src/core/*generated.h`). The copies are checked against the
live code at startup (FNV-1a) and in the emulator against the stock function.

**Coexistence and safety.**
- Writes use one `lock cmpxchg8b` on an 8-byte window inside a cache line. Each site is classified
  stock / ours / foreign; a foreign write (another mod) leaves that group alone and is logged.
- Failure containment: a fault in a hook is caught (SEH), switches the feature off for the run and
  restarts the patch round without it ("isolation", `registry.cpp`). Since 1.0 the native layouts
  (colours, PegSet, GuitarStrings, FretGrid, pitch, next-note grid, publication, tuner, C-lite) are one
  unit without a legacy fallback (`Rs7_IsSevenUnit`): a member that fails at startup is handled like
  a core failure - nothing of the main batch is written and the game stays stock (six strings; the
  plain-PSARC unit is separate). Other groups (diagrams, chord loops, vguitar, diagnostics) are still
  isolated on their own. A loss at runtime (the deferred FretGrid write) only clears the switches the
  stubs read, and its dependants (publication, tuner, C-lite) with it.
- Feature switches live in `rs7string.ini` (see `docs/release/SETTINGS.md`).
- RSMods hooks d3d9 function bodies, not vtables, so both hook orders chain. RS_ASIO and RSMods were
  tested together with the mod.

## 3. The string-count model (C-lite)

The string count is a property of the guitar **build**. `Rnd_GuitarProfile_Load` `0x593B00` writes it
(site GKC), the stock "profile changed, rebuild the guitar" path (the one guitar <-> bass uses)
builds a guitar with it (GKE compares the count, GKT triggers the rebuild at song selection, GKM
remembers the guitar manager and the player). `InstrumentInfo +0Bh` holds the count for the guitar's
whole life, and everything the guitar shows (7th string line, 7th peg, wider fret bars, edited
headstock, tuner strings) follows it. The rebuild takes a few milliseconds. Nothing is published at
song setup, switched in the tuner or restored at song end. Selecting a 6-string song, or entering
Session Mode, Guitarcade, Lessons or the technique hub, rebuilds to 6.

Still per song (S4 `0x7DF0FC`, `Rs7Song_WidenDetector`): the detector's active count and lowest open
note, only for a 7/8-entry tuning on a guitar built with that count.

Decisions:
- **Native layouts instead of per-feature extrapolation.** The stock arrays are widened to
  capacity 8 and the stock code runs over them (GuitarStrings, PegSet, colours, FretGrid, pitch,
  next-note grid, publication ports, tuner; chords behind `[chords] native_chordloops`). C-lite needs
  the native tuner and the publication ports; they need the others. Since 1.0 these are one unit
  without ini switches and without a legacy fallback: a member that fails its startup check leaves the
  game stock (section 2). The older per-feature paths (`native_*=0`, `clite=0`: the S7 string object,
  the stage-2 peg, the six-column colours, the B-phase tuner, the six-row grid with R7b / N9) were
  removed; only the per-song arm of N5 (S4 marks) remains for a runtime loss of the ports.
- **No SEH around the rebuild.** A fault half way through would leave a half-built guitar that the
  game keeps using, so it goes to the process's exception handling (since 1.0 there is no `clite=0`
  switch to fall back to: removing `dinput8.dll` turns the mod off).
- Player 1 (second player) is never built with 7/8: GKC writes 6 for every player but 0 (local co-op
  limitation).
- **A guitar built with 7 must not reach stock readers of `+0Bh`.** A sweep of the executable found
  108 byte reads of `+0Bh` in 61 functions. Verdicts (CONFIRMED statically): 53 functions safe
  because a native copy or port runs; 4 alias inside the GuitarStrings object (cosmetic);
  `Rnd_Headstock_LoadStringMeshes` `0x58E090` overflowed the 0x38-byte headstock object (crash on a
  6 to 7 rebuild, fixed by sites GKH1 `0x58E1BA` / GKH2 `0x58E36D` clamping both loop bounds to 6;
  the 7th segment `string_size_7` is handled by the asset code); three Session-Mode harmony readers
  (`0x4435F0`, `0x444C30`, `Rnd_ScaleChordMarkers_Update` `0x445740`) index `[6][26]` arrays with
  `+0Bh` and overflow, which is why entering Session Mode rebuilds the guitar to 6.
- Lua `GetGuitarStringCount` `0x77CE50` (site GKL) answers 7/8 for such a guitar. HYPOTHESIS: Lua
  tuner/diagram code loops over it; not verified.
- `GridBuild` waits up to 5 s for the FretGrid layout; a timeout completes the load but marks the
  neck FAILED, and the detector then stays at six strings (logged). Known limit: re-selecting the
  same 7-string width does not rebuild (stock `Equals` sees equal profiles), so the failed mark stays
  until a 6-string selection.

> [!NOTE]
> Not run in the game: 8 strings (`[neck] eight=1`), two players, Session Mode after a 7-string song.

## 4. Native capacity-8 layouts

All rows are members of the native 7-string unit (no ini switch since 1.0; `Rs7_IsSevenUnit`).

| Area | Runtime switch | What changes |
|---|---|---|
| GuitarStrings | `g_rs7_native_strings` | capacity-8 arrays (`+40h` main, `+60h` ghost; the stock `+4`/`+1Ch` six remain as aliases), InstrumentInfo grown to `0x48` with its own counts (`+40h` active, `+41h` loaded; `+0Bh` stays the stock count). Bind, load, unload, fade, transforms, update, reset, pluck, vibration, InitColors, free, section dimming/glow choice `0x460550`, glow ramp `0x5441A0`, headstock segment colours `0x58E4F0` run as relocated copies (GS* sites). Songs start/end hooks GSSS/GSSE. |
| PegSet | `g_rs7_native_pegset` | 8 slots; peg 7 is slot 6, so stock fade/layer/tint loops cover it. |
| Colour manager | `g_rs7_native_colours` | `*0x135F58C`: 6 slots per colour state widened to 8 (both colour modes). |
| FretGrid | `g_rs7_native_fretgrid` | cells of 264h per row instead of 1CCh; hooks FGR*, PUBGC/GO/GB/GA/GF/EDGE/GP. Answers `Rnd_FretGrid_GetCellPos` `0x58FE00` from native cells for strings 0-7. |
| Pitch | `g_rs7_native_pitch` | detector grown to `13E0h` (int16 tuning8 `+13B8h`, openMidi8 `+13C8h`, active count `+13D8h`), matcher to `6Ch`; `0x873E90` replaced by a port with an 8-entry base table (35 / 30 for strings 6 / 7); strings >= 6 of `0x873F80` calls go to `Rs7_Pitch_Raw8`. |
| Next-note grid | `g_rs7_native_nextgrid` | 8 time rows (sites NGB*). |
| Tuner | `g_rs7_native_tuner` | see section 8. |
| Chords | `[chords] native_chordloops` (not a unit member), N-slot sites | see section 7. |

Stock fixed-6 readers that were judged cosmetic or guarded and are not ported (HYPOTHESIS: none
indexes past its array): renderer glow `0x54424B` / glow colour `0x544366` for strings 6/7, BD1-BD4
bend notation for string 7, mode readers `0x59A920` / `0x7A6390`, chord residual loops `0x7E397A`,
`0x7E3C57`, `0x7E42A1`, `0x7A6280`.

## 5. Rendering the seventh string

**Lane and gems.** String 6 is a real FretGrid cell; its lane lies above the low E lane
(`z = 2*z(string 0) - z(string 1)` before the native grid). Gem colour (`0x7A8590`), NoteFx
sparks (`0x40CAD5`), hit/miss text (`0x7F2FDD`) and the Riff Repeater marker (`0x79BBFD`) read
string 6's colour from the native eight-column colour manager (CM* sites; the six-column fallback
C1-C5 was removed in 1.0). Note tails (sustain ribbons, slides, vibrato, bends) follow the 7th lane
because the native grid gives string 6 a real cell; BD1-BD4 give string-6 bends string 0's direction.

**Colour.** The game colours a note by sampling a 256x128 ramp texture (16 rows, one per string and
colour mode) at a V coordinate. Step 1 writes the exact `[color]` RGB for sparks and text. Step 2
(`ramp_swap=1`): a worker walks the engine's texture list under the game's lock (list head
`0x1360B00`, lock `0x1383D74`, `next` at `+0x34`, renderer data `+0x2C`, the `IDirect3DTexture9*` at
renderer data `+0x64`), collects `note_gradient_map.dds` / `gradient_map_additive.dds`, patches slots
64/65 (`GetTexture` / `SetTexture`) of the game device's vtable about 15 s after `d3d9.dll` appears,
and on the first bind copies each ramp into a same-size MANAGED texture where only the lines at
V6 = 57/128 (normal) and 121/128 (colourblind) are repainted. A self-check (CPU plus read-back)
must pass before string 6 switches to V6; anything unexpected keeps V6 at 0 so string 6 shows the
stock grey preview row and strings 0-5 stay stock. d3d9.dll gives each device its own vtable copy and
rewrites it at times (e.g. at song load); the worker re-checks every second. Limits: with 3 or more
mip levels the colour is pure only on mips 0-1; RSMods' custom note colours override string 6 with
its row 7.

**7th string line** (`string7_line`, `src/core/gsnative.*`, per-guitar state in `src/core/string7.*`): string 6
is a stock 0x110 string object of the native GuitarStrings (slot 6 of its capacity-8 arrays, ghost included),
driven by the stock string loops; atlas row V = `[neck] string7_v` (default 0.4 = string 0's row; 0.0-0.3 are
the bass rows). The game's own section dimming lights it (SNG section string mask bit 6: N12 `0x55CC71` / N13
`0x55CE3A`); bends N18-N23 (chart bend on the ghost string `ghost_up_NN`, detected bend `bendup_NN` through
`Rnd_GuitarString_SetBend` `0x598910`) and the chord pluck PL1 reach the native string 6. The stage-1
DLL-owned string object S7 (with its own placement, colour, glow, noteway draw-list fix and GPU diagnostic:
N2-N4, N6, N7, N10, N11, N14-N17) was removed in 1.0. Limit: after `Rnd_GuitarStrings_ResetState` `0x59AC20`
(restart/seek) a running ghost bend is only released by the next string-6 note end.

**Fret bars** (`src/core/frets.*`, F1 `0x58FB20`): on a guitar built with 7/8 strings every bar (and
the nut) moves half a string spacing towards string 6 and is scaled uniformly by
`k = (L + d/2)/L` with `[neck] fret_halflen` L (4.61, the mesh's half length); measured d 1.6079,
k 1.1744. Bars are about 17 % thicker. Locators and span data are never written. Chord frames
(B1 `0x7A2BCA`, B2 `0x7A91E4`) get a string-axis scale and shift from the guitar's lane positions.

**Headstock model** (`src/core/assetload.*`, `hsbuild.*`, `[assets] headstock7`): the guitar and its
frame entity load once at boot, so the 7-string model (brandless "Facet", authored for this project)
is merged next to the stock one: when the game's nif handler (AL1 `0x4A8560`) loads a guitar frame,
the model is loaded with the same handler under the same resource name, renamed `rs7_alt`, culled
and attached as the last child of the stock top node. The stock part comes first, so name lookups
(`string_size_%d`, `Locator_Peg_%d`) still bind stock nodes. While the guitar shows the 7th string
`rs7_alt` is shown and the stock sibling culled; the six segment param objects are pointed at the
model's `string_size_1..6`; `string_size_7` gets its own params and the 7th string's colour every
frame (as `0x58E4F0` does for the stock segments) and its nut end is lowered 0.3 into the nut; the
PegSet's locator pointers move to the model's `Locator_Peg_1..6`, and `Locator_Peg_7` carries the 7th
peg and the tuner's B letter. The model's own textures are registered through the game's dds handler
(`0x4A81D0`, NiFile adapter ctor `0x49FF90` over an in-memory IFile) under the stock folder's
names. `headstock7_wood` darkens the wood texture's DXT1 end points (the game shows the texture
unlit, so the texture is the only brightness control).
- **Self-build.** The `.nif` files are not shipped. At the first guitar-frame load the mod builds
  them from the user's own installed game: `data/headstock/rs7string_headstock*.bin` is a versioned
  copy/insert program (`RS7H1`, hex framed) that is applied to the game's own template NIFs
  (identified by SHA-256; unknown templates are refused, never patched heuristically). The package
  holds only authored geometry, node edits, copy ranges and hashes, plus six procedural `rs7hs_*.dds`
  textures; template bytes are copied at run time. The generator audits that no 16-byte run of either
  template occurs in the literals. A build takes about 0.2 s inside the loader thread; existing files
  are never replaced; failure keeps the stock headstock. The left-handed frame
  (`epi_whilshire_cherry_lh.nif`, the right-handed model mirrored in NIF Y; CONFIRMED by comparing the
  files) has its own package and artifact `rs7_inline7_lh.nif`. Offline regeneration:
  `tools/gen_headstock.py` (needs the maintainer's extracted templates and pipeline outputs).
- **Layouts** (`kLayouts` in `assetload.cpp`): "6 in Reihe" (`guitar_standard_inline`, stock part
  `Wilshire_RH/LH`, pegs `epi_whilshire_peg.nif`) and "3+3" (`guitar_standard/gib_lespaul*.nif`, pegs
  `peg_bakelite.nif`). With 3+3 the in-line model sits correctly on the Les Paul neck; the pegs
  follow the profile option, not the frame, so the mod swaps in the in-line peg model
  (`Rs7AssetDump_ReadInlinePeg`), and `UI_PlayerTuner_PlacePegLetters` `0x5B1A10` would turn letters
  of pegs >= count/2 round when the InstrumentInfo in-line byte (`+0Ah`, from profile `+13h`;
  `+8` = InvertGuitarStrings, `+9` = LeftHandGuitar) is 0, so LT1 `0x5B1B53` (and the same edit in
  the native tuner's copy) reports such a guitar as in-line (CONFIRMED in game).
- **Peg and knob tint.** The 7th peg is slot 6 of the native PegSet, so the stock peg highlight pass
  `0x543150` tints its knob like the others (the stage-2 mirrored peg and its PH1/PH2 tint hooks were
  removed in 1.0).
- Limits: the stock headstock shows for about 0.1 s when the pre-song tuner hands over to the song;
  the 8-string model (`rs7_inline8.nif`) is not used; with the reversed string order the headstock
  strings 1-6 fade out before the posts do.
- Unverified input: an arbitrary custom NIF configured through `headstock7_file` is passed to the
  game's NIF handler after a size check only (HYPOTHESIS: a malformed file can still damage the stock
  parser).

<details>
<summary><b>Reference counts</b></summary>

Edited-model holders own up to 117 references (`altHold[6][9]`, `params7[9]`,
`stockHold[6][9]`). Guitar release destroys the headstock BEFORE `Rnd_GuitarStrings_Free` `0x599D70`
(CONFIRMED `0x58D9E0`, `0x592770`). NIF/DDS handler wrappers start with a zero count, vtable `+4` is
AddRef (`0x88A200`) and `+8` Release (`0x8A53D0`); calling NiObject DeleteThis on them calls AddRef.
`Rnd_SetStringShaderParams` `0x5998F0` owns nine output slots (assignment increments the LONG at `+4`,
zero calls vtable `+4`); the stock holder destructor `0x58DAB0` releases them last slot first.

</details>

## 6. Detection and scoring

- S4 `0x7DF0FC` reads the SNG tuning at song setup (`Play_Detector_SetTuning` `0x4E263B` exit S3 sets
  the lowest open MIDI `35 + t6`); S2 `0x873E9F` / S1 `0x873F98` and `0x873E90` give the expected
  pitch (see section 4 for the native storage).
- The single-note matcher is lenient by stock design (a near-pitch fallback `0.5 +
  Play_Matcher_PitchTolerance`, history-indexed read at `0x4E5A61`); string-6 notes never false-hit
  in the virtual-guitarist runs, but stock strings can with a wrong played pitch.
- The detector's floor (MIDI 24, `0x4DBF73`) is unchanged; guitar tunings stay above it.
- Pitched slides are credited when the glide nears its target; unpitched slides at the onset.

## 7. Chords with the 7th string (native N-slot)

`src/core/nslot.*`, `nsmatch.*`, `nstracker.*`, `src/hooks/stubs_nslot.cpp`. The game's own chord
code runs over N-slot data for every song (6-string and bass songs get the same records with slots
6/7 empty).

- **Data.** At song load each chord template becomes a T8 record (stock `0x48` bytes plus
  `frets[8]`, `fingers[8]`, `midi[8]`) and each ChordNotes record a C8 record (stock `0x948` bytes
  plus `mask[8]`, `slideTo[8]`, `slideUnpitchTo[8]`, `vibrato[8]`, `bends[8]`) in relocated per-song
  arrays (XB1 `0x55922C` at load, PP1 `0x569137` after `SNG_Song_PostProcess`, SF1 `0x558686` frees).
  The stock stride sites (`begin + id*48h` / `948h`: TP1-TP12, CP1/CP2) hand out the superset
  records. Slots 6/7 come from the RS7X block; packages without it fill slot 6 from the template's
  old name byte.
- **Detection.** `Play_Matcher_DetectChord` `0x4E7B30` (MD1) takes int32 targets[8]; up to six runs the
  stock body bit for bit, seven or eight an exact port (`nsmatch`, the game's sort and subharmonic
  table). Chord requests (`0x7B8820`), the Lua binding, mistake analysis, the whole-chord tracker and
  the eval wrappers all pass int32[8]; strings 6/7 live in the template copy's unread name area
  (BT1/CS1). Sustained/technique chords get child evals for string 6 (AG1-AG3).
  `[chords] wide_misses` (default 2): 7/8-note chords may miss up to that many notes on any string;
  5/6-note chords one, 2-4-note chords none (stock). With 2, a 7-note chord passes without its low B
  by design.
- **Rendering** is the game's own chord code over the N-slot arrays (byte patches NR*, NS*, CT*, SH*,
  HS*, NN*, NK*, NM*, FG*): gems on the 7th lane, the open-string bar for an open string 6, tails,
  slides, bends, vibrato, fret labels, finger numbers, next-note neck targets (NG9, native grid), section
  string mask, the double-stop variant (one stock string plus string 6), and chord pluck PL1 `0x7E2EFE`.
- **Regression record** (virtual guitarist, lead 40 ms): RS7 Test 36/36, runs across 7 strings 55/55,
  techniques A 24/24, M4-1 pairs 25/25, three-note chords 15/21 (same as stage 1), full 7-note chords
  16/16 with `wide_misses=2`; 6-string controls unchanged. Techniques on string 6 render in the 7th
  lane; their detection (bend target, slide end) is lightly tested.

**RS7X extension block** (`src/core/extblock.*`, site XB1; format: [RS7X-SPEC](release/RS7X-SPEC.md)).
Strings beyond the six stock slots live in a block appended to the SNG body after MetaData, inside the
zlib stream. The stock loader never reads it (CONFIRMED: `SNG_Song_Load` `0x558BD0` only checks the
inflated size against the container's size field and returns after MetaData without comparing the
stream position). XB1 hands the stream to `Rs7Ext_OnSngLoaded`, which validates it against the loaded
song and keeps it per song; the N-slot records are built from it. An open extended string gets a
ChordNotes record with its OPEN bit like an open stock string; a chord of one stock string plus one
extended string is a double stop.

## 8. Tuner and tuning names

**Native 8-slot tuner** (mandatory since 1.0, `src/core/tuner8.*`, `gen_tuner8s.py`; the six-slot B-phase tuner was removed). Sessions are
allocated with `380h` (TU8ALLOC/TU8INIT/TU8FREE); a 7/8-string target (manifest keys `Rs7String6`,
`Rs7String7`, or a published count) makes the session native at TS `0x5AF200`: its six per-string
arrays move to a capacity-8 tail, strings 7/8 are slots 6/7 of every native layout, and the tuning order
is lowest first (6,0..5 / 7,6,0..5). 21 `UI_PlayerTuner` methods run as relocated copies (TS8* sites,
emulator-checked against the stock functions), 10 mid-hooks cover single reads. Decisions:
only 7/8-string sessions become native; physical string indices plus an order table instead of
re-indexing by rank; the session's count, never `InstrumentInfo +0Bh`, drives the tuner. Open
(HYPOTHESIS): Wwise tuned-sound events `Guit_Pur_<note>_Tuned` for strings 6/7; arrow-key direction for
left-handed profiles; two players sharing the two extra label strings.

**Manifest key.** `Tuning` objects carry `Rs7String6` (the 7th string's offset `t6`, `Rs7String7` for
the 8th); add it with `tools/manifest_key.py IN.psarc OUT.psarc [--t6 N]`. Never use the key
`string6`, which crashes the song list.

**Low range** (`[tuner] lowrange=1`, opt-in): the stock "wrong instrument" check
(`UI_PlayerTuner_CheckWrongInstrument`, site W `0x5B2DDE`, `fcomp float [34.0]`) uses a session
threshold `min(34, lowest open MIDI - 1)`; the detector's lowest-open filter (`+11F8h`) is lowered on
each selection of a 7-string session. A bass B0 (MIDI 23) is not handled.

**Tuner arrow keys** switch strings only in the precision view (a played note or Space brings it up);
the stock 6-string tuner behaves the same.

**Tuning names** (`[tuner] names=1`, `src/core/tuningname.*`). For an arrangement whose `Tuning` has
`Rs7String6` the song list row, song/arrangement pages and the tuner show a 7-string name: for strings
0-5 all at k and the 7th at k: `+1 C Standard`, `0 B Standard`, `-1 Bb Standard`, ... `-9 D Standard`;
7th at k-2: `C Drop Bb`, `Drop A`, `Bb Drop Ab`, ... `D Drop C`; plus Extended Drop D/C/B (`-2 0 0 0 0 0 |
-2`, `-4 -2 -2 -2 -2 -2 | -4`, `-5 -3 -3 -3 -3 -3 | -5`), Double Drop D (`5 5 5 4 3 -2 | 3`) and Open G7
(`-2 -2 -3 -5 -6 -9 | -4`; the last two registers are a HYPOTHESIS). Names follow the game's spelling
(flats, upper case, its accidental glyphs) and carry the mark `[tuner] name_suffix` (default `7Str`) /
`name_prefix`. Other tunings on a 7-string arrangement keep the game's name with the mark. Sites: TJ
`0x56C950` (manifest name resolver), TU/TUT `0x56C300` / `0x56C408` (arrangement UUID resolver), TN
`0x5B7750` (tuner offset-array resolver). They read the key and the six offsets, call the original, and
then assign the display output (row `+138h`) and raw token (row `+150h`). English only (plain text, no
`$[id]` tokens; the game localises tokens through `0x7BF420` from `maingame.csv`). The tuning filter
(`0x67A630` / `0x4ED5B0`) and sort (`0x575F60`) stay stock and see only the six-string offsets, so a
7-string song passes the E Standard filter and sorts among 6-string songs; group headers
(`0x6590C0` / `0x680170`) can show a 7-string name above 6-string songs.

## 9. Chord diagrams (`[chords] diagrams7`)

The chord book, song panel and lesson scene draw chord diagrams from Scaleform movies
(`gfxassets/libs/commonlib.gfx`, `gfxassets/views/contentpanel.gfx`, `gfxassets/views/ge_game.gfx`).
Seven-row diagrams need a changed ActionScript class (DoInitAction tag, type 59, class sprites
1103 / 28 / 26) and extra data keys.

- **Data delivery.** DGM `0x757698` (tail of `0x757030`) and DGS `0x7580DF` (tail of `0x757960`) add
  `LowBFret/LowB_Finger/LowB_Color` and `LowFsFret/LowFs_Finger/LowFs_Color` (colours `0x808080` and a
  provisional `0x00FFFF`) from the native T8 record after the stock serializers have run, recomputing the
  fret window over N strings. TP13 `0x7578BC` routes the loaded-song template lookup through
  `Rs7_NsTplAt`; DGB `0x55A02C` binds PostProcess's UUID-selected attributes to the exact SNG song.
  Six-string and bass shapes emit no changes. Not covered (stock output): generic database chords,
  catalogue songs whose SNG is not loaded, missing RS7X/CHDT blocks, mismatched six-column projections.
- **Movie swap.** The Scaleform `FileOpener::OpenFile` slot (data word `0x121F2D0`, vtable `0x121F2C4`
  slot 3 = `0xC30330`) is wrapped at run time. For a target url the hook works on the returned wrapper
  (`W+8` = NiFile adapter, adapter `+0x18` = IFile), verifies the SHA-256 of the user's original tag,
  reconstructs the replacement tag from the recipe, verifies its SHA-256, and installs an in-memory
  IFile. Any failure returns the wrapper untouched and logs `diagrams: refused ...`. CFX/CWS compressed
  movies are refused.
- **Recipes** (`data/chord-diagrams/*.rs7da`, generator `tools/diagram_patch.py`) contain only COPY
  operations (offset, length), literals that do not occur in the original tag, lengths and hashes; no
  Ubisoft bytecode. They are specific to one movie build. The ActionScript extension that produces
  the edited class is `tools/gfxpatch` (offline, needs the user's movies and FFDec).
- Hand-illustration fields still use the stock five-finger summaries; the eighth row is data-supported
  but not gameplay-supported.

## 10. Plain (unencrypted) PSARC loading

`[psarc] plain=1` (`src/core/plainpsarc*.cpp`, sites group `psarc`). Three sites, CONFIRMED by emulator and in
game: PP_SNGD `0x558CD4` (the call of the SNG AES-CTR decrypt `0xE3EA10`: for magic `0x4A` + flags 1
copy the payload after the 16-byte IV slot, else call the game's decrypt), PP_SNGF `0x558C1A` (the
`cmp [esp+68h],3` flags check: flags 1 pass), PP_TOC `0x8A3643` (start of the TOC decrypt block
`0x8A3640..0x8A368F`: archive flag bit 4 clear means a plain TOC). The CDLC enabler still provides the SNG
signature bypass (`0x5582F0`); rs7string does not touch it. If any of the three sites is foreign (the
enabler's plain-PSARC build wrote it) all three are left alone and `plain psarc: provided by the CDLC
enabler` is logged. An enabler that loses the race prints "Unencrypted CDLC will not load" although
plain packages load; cosmetic. Matrix (2026-10-14): plain and encrypted packages and official DLC
play; with `plain=0` and an enabler without plain support the game aborts after the profile is chosen.
Open risk: locked 8-byte exchange is atomic for data, but cross-modifying code needs serialization
on the executing core (same assumption as the upstream enabler's patch; no SNG is loaded at patch
time).

## 11. Window, input, overlay, logging

**Console QuickEdit.** `[window] console_quickedit_off=1` turns QuickEdit off on the console the CDLC
enabler opens: a click starts a selection and every console write then blocks; RSMods logs to stderr
on the game's main thread, so the game froze until Enter.

**Background mode** (`[window] background=1`). `Win_GameWndProc` `0x78C9B0` tests `WM_ACTIVATEAPP`
at `0x78C9DC` and posts `0x7E9` on loss (CONFIRMED). The window subclass (installed at window
creation, independent of the overlay) swallows `WM_ACTIVATEAPP` with `wParam == FALSE` (returns 0),
`WM_ACTIVATE` with `WA_INACTIVE`, `WM_KILLFOCUS`, and `WM_NCACTIVATE` with FALSE (returns TRUE); it
never synthesizes activation. The foreground query (`GetForegroundWindow` as called from the exe on the
game thread, e.g. in `0xEC5D80`) is hooked to report the game window; other callers (overlay, DirectInput) see the
real value. HYPOTHESIS: the loss notification leaves audio/session state suspended, which was the
reported "song stops after seven seconds" symptom.

**Overlay** (F9, Dear ImGui 1.92.x, vendored unmodified in `third_party/imgui`): hooks vtable slots 16
(Reset) and 17 (Present) of the game's device, not EndScene (RSMods inline-hooks EndScene and skips its
own menu for calls not from the game exe). It draws before the game's Present in its own
BeginScene/EndScene with render target, depth buffer and viewport restored. A fault in its callbacks
switches it off. Input: the game window is subclassed at the first Present; the game's DirectInput still
sees the devices (a click can reach both). See [DEVELOPMENT.md](DEVELOPMENT.md) for the command pipe.

**Logging.** `rs7string.log` next to the DLL: patch results, per-song mode, ramp copy, feature lines.
Song end log (`[debug] song_end_log=1`): sites SE1 `0x7DD600`, SE2 `0x7DFA30`, SE3 `0x7E0AE6` log each
session state change; a song leaves state 3 (playing) to 4 (end) in exactly three ways: an end-song
event (type 3), the leave-state call, or a NULL song object while playing. A stray Enter about two
seconds into a song ends it about 7 s later through the first route with the song clock still at 0
(game behaviour; HYPOTHESIS: also stock).

## 12. Virtual guitarist (test mode, `[test] vguitar=1`)

`src/core/vguit.*`, `vgsynth.*`. Plays the chart into the game's own input path with a synthesized
guitar and logs how the game judged every note, so charts can be tested without a guitarist. TICK
`0x7E2880` copies live note objects (`session+41Ch`, about 3 s ahead) into a schedule; AUD1 `0xC4618C`
(`SndIn_PA_Read`, inside the input device `Read` `0xC460C0`) writes synthesized samples into the 48 kHz
mono float input before the stock callback feeds detection; JUD1 `0x7E2CFF` / JUD2 `0x7E6AD0`
record the judgement per note to `rs7string.log` and `rs7string_vguitar.csv`. Voices are
Karplus-Strong with a time-varying fractional delay; the excitation is one period of the
plucked-string spectrum (a first version used noise, which gave random harmonic levels and made low
notes late or missed). Measured: injected-onset detection latency about 42 ms, Wwise buffer 128
frames. Needs an open input device (RS_ASIO with the interface is enough). Limits: one player,
tremolo is one pluck, harmonics play the fretted pitch, a Riff Repeater loop reschedules only after a
backwards time jump.

## 13. Eight strings

> [!WARNING]
> `[neck] eight=1` is experimental and not release-tested.

The native layouts, tuner, tuning-name families (e.g. `F# Standard`-style 8-string names, `8Str` mark) and chord data have capacity for eight.
Missing for real 8-string play: eight-wide variants of 25 seven-only sites (render, fingers, section
masks, tracker), the 8th peg (PegSet capacity, headstock Raise/Frame, peg lifecycle), an 8-string
headstock model with `Locator_Peg_8` and `string_size_8`-style material parameters, a colour for string
7, virtual-guitarist 8-voice chords, in-game acceptance.

- Chord frame height is not native: `Rnd_GemGeom_ApplySpan` sets the string-axis scale to 1 and derives
  only the fret-axis extent, no count parameter exists on that path, so B1/B2 scale the frame from the
  lane positions (HYPOTHESIS: an 8-string neck keeps the 7-string lane spacing).
- Single-note bend release for string 7 (N19 `0x7E4059`, N20 `0x7E43DB`, `0x7E442E`) is not ported.
- Several N-slot tracker guards (CN2/CN3/CC2/BF1/CH2) still need widening for string 7.

## 14. Known limitations and open risks (1.0)

> [!NOTE]
> From the 1.0 release-candidate reviews. Items marked "open" had no fix at that time and were not
> re-verified for this list.

- Local co-op: only player 1 gets a 7-string guitar.
- Stock threads versus patch rounds: patch rounds and rollback are not a transaction with executing
  game threads (open); isolation does not report rollback failures separately (open); the N-slot song
  store and the 7th-string render publication can outlive immediate song-store reclamation (open).
- Some settings are read through permissive `GetPrivateProfileIntW` paths (`startup.cpp`,
  `window.cpp`), so values outside the documented range can still enable a feature the UI reports as
  off; settings text is narrowed to ASCII when edited in the overlay.
- Unfixed leaks (small, per guitar or per ramp fault): edited-headstock holders can lose retained
  references after a frame replacement; a faulting ramp path cannot release local COM ownership.
- Rare: a `GridBuild` timeout leaves the neck marked failed until the next 6-string selection.
- Tuning names are English only; the tuning filter and sort use six-string offsets.
- The overlay does not block DirectInput; its device-Reset path is unit-tested but was not hit in game.
- Real-guitar bends, slides and harmonics on string 6 and chord techniques still need a final check.

## 15. Mid-hook contract and cold-site conversion

Design record of the generic mid-hook (`src/hooks/midhook.*`) and of converting hand-written naked
stubs to it. Tags as above; measurements are one machine and one microbenchmark.

**Contract.** `Rs7Mid::Context` is 44 bytes: EDI, ESI, EBP, original ESP, EBX, EDX, ECX, EAX, EFLAGS
(nine dwords), then a private callback slot and the writable `jump`. Compile-time offset assertions
bind it to the thunk. GPRs, ESP, flags and `jump` are writable; the callback is `cdecl`, must return
normally and must not throw. Segments are the process ABI. x87/MMX/SSE/MXCSR are saved with FXSAVE
and restored with FXRSTOR, so a callback cannot change the resumed floating-point state (AVX upper
halves and privileged state are outside the game's ABI). The generated 15-byte entry pushes the replay
address and the callback, then jumps to the shared thunk: it saves GPRs and flags, copies the context,
aligns a 512-byte FXSAVE area, clears DF, initialises x87, calls C++, restores floating-point state
and copies the edited register frame under the requested ESP (direction-aware for overlapping
frames), then POPAD/POPFD/RET. ESP is the site's pointer, not PUSHAD's; at least 40 writable bytes
below the requested ESP and enough stack for the callback are required. Bytes below the live stack are
scratch for both the thunk and the old stubs, so equivalence compares live memory only.

`jump` starts at the relocated copy of the displaced instructions. Leave it for normal replay; set it to
`site + length` to skip the replay (the callback then does the stock work itself, including any
`push`), to a branch target of the displaced code, or to another validated continuation. Never target
the middle of an instruction or the hook's own entry. A trailing displaced `RET` already returns
directly. `Context::Compare32(lhs, rhs)` reproduces the arithmetic flags of `cmp` and keeps every
other flag; use it when the stub being replaced left comparison flags that a continuation could read.

**Relocator.** Whitelist only (see section 2): rejects prefixes, `RET imm`, indirect transfers, unknown
opcodes, branches into instruction interiors and internal calls; widens relative branches and remaps
internal ones; no blind-copy fallback. `tools/sitecheck.py` still checks whole instructions and inbound
branches. A relocated `CALL` runs from the thunk's copy, so the callee sees a different return address
than at the original site (stubs that tail-jump into the callee kept the original one). Live unload
needs thread quiescence and is unsupported.

**Pilot (SE1/SE2/SE3, song-end diagnostics) - CONFIRMED.** The old stubs `cmp`'d `g_rs7_endlog` before
saving anything, so the callback reproduces those flags; the logger takes `Context` plus the original
stack pointer (never `&ctx.edi` as a PUSHAD frame: the private fields would pose as the return address
and argument). 180 old-vs-new Unicorn comparisons (3 sites x 5 setting values x 3 incoming flag
patterns x 2 stack alignments x 2 helper behaviours) pass.

**Conversion method.** For every converted site the old compiled stub is frozen (`tools/freeze_stubs.py`
from a DLL built before the change: bytes plus symbolic fixups, verified by re-linking; no game
bytes) and `tools/tests/test_midhook_cold.py` / `midhook_oracle.py` run it and the production
entry + callback on identical machine state in Unicorn. Compared: all eight GPRs, EFLAGS, the exit
taken (final EIP), x87 status/top/ST0-1, XMM0/XMM7, the live stack from the first pushed byte up, a
data page, every other machine write, exported switches, and the complete input of every helper call
(arguments, pointed-to bytes, order). Helpers are replaced by `ret` that dirties EAX/ECX/EDX, flags and
XMM, and return configurable values so both branches of each site run. Each case runs with three
incoming EFLAGS patterns (one with DF) and two stack alignments; switches use noncanonical values
(2, 0x80000000, 0xffffffff) too. Flag contract: the arithmetic flags equal the old stub's where its
replayed or compared instruction writes them ("exact"); where the old stub left helper-dependent
flags that no continuation reads ("dead") the hook leaves the incoming ones; every non-arithmetic bit
(DF, IF) is always the incoming one, which the old stubs did not guarantee. The frozen file is
`tools/tests/fixtures/midhook_cold_951f935.json` (stubs as built at `951f935`).

**What converts and what stays.** HYPOTHESIS (estimate, not measured runtime frequency): load, free,
setup, song-start and teardown hooks with straight replay run rarely enough that the thunk's
fixed cost is irrelevant. Hot per-note, per-frame, lane, grid, colour, pitch and audio-thread hooks stay
hand-written until profiling justifies full-context overhead. Also stay hand-written: wrappers that call
the original and do work after it returns, callee `RET imm` cleanups and epilogues, replaced `CALL` sites
that tail-jump, sites whose displaced bytes use a prefix or INT3 padding, custom saved-frame consumers,
stack probes and return-address-sensitive callees (table below). A converted site either leaves the
replay alone (the callback only observes or calls a helper) or skips it (`jump = resume`) and performs the
stock instruction's effect itself; where the old stub left compare flags, the callback reproduces them
(`Context::Compare`).

**Cost.** Native QPC microbenchmark (`tests/test_midhook.cpp`, 7 x 300,000 no-op hits, three runs of the
final build): 140.8-143.2 ns added per hit before callback work (stock 2.2 ns, no-op hook 143-145 ns),
even with the feature switch off (the thunk always saves state; earlier builds measured 150-190 ns). Not
frame time, and the callback's own work is extra (a 64-entry table scan for the PegSet sites, a short
loop for GSSS/GSSE). At 10,000 hits per frame that would be about 1.4 ms per frame, hence the cold-only
rule. HYPOTHESIS (hit counts not measured in game): the converted sites run a few to a few dozen times
per guitar load, tuner session or song (PSM2 at most 8 per renderer setup), i.e. well under 10 microseconds
per song.

**Conversion status.** 25 sites were converted from naked stubs (the SE pilot's 3 and 22 cold sites); 3 of them (N2, N3, N7) went with the legacy S7 object in 1.0, as did 37 hand-written stubs. "stub" = still hand-written.

| Family | Sites | Status |
|---|---|---|
| song end log | SE1-SE3 | mid-hook (pilot) |
| tuner session / tuning names / SNG load | TK, TS, TUT, XB1 | mid-hook |
| pool request counts, GuitarStrings / InstrumentInfo / PegSet free sizes | N1, N21, GSZ1, GIZ1, PSA2 | mid-hook |
| native PegSet destructor / unload / ctor / load | PSD1, PSD2, PSU3, PSC1, PSL1 | mid-hook |
| native PegSet mesh latch / colour reset / colour name; GuitarStrings dimming at song start / end | PSM2, PSR1, PSN1, GSSS, GSSE | mid-hook |
| relocator rejects the displaced bytes | S3 (operand-size prefix), PSU1, PSU2, PSM1 (operand-size prefix on `cmp r16,[mem]`), and TF1, W, V2, CP1, CP2, BT1, CS1, CC3, UP1-UP3, PD2-PD5, PT1, NG1, PUBEDGE, CLBM, CLBA | stub (measured before the 1.0 removals: `Rs7Mid_Build` over the game bytes rejected 29 of 246 jump sites) |
| call-kind sites (the mid-hook writes jump sites only; a relocated call also changes the return address) | S4, SF1, PP1, PP_SNGD, PSL2, PSN2, IN1, B2, AM1, EV1-EV5, PC1-PC4, TM1-TM6 | stub |
| wrappers that run the original and add work around it, or replace an allocator call | F1, TJ, TU, TN, GSA1, GSC1, GIA1 | stub |
| per-frame, per-note, audio thread, copied-function entries, custom conventions | R2-R6, BD1-BD4, V2, S1, S2, I1, N5, N12, N13, N18-N20, N22, N23, PL1, GS* entries, FGRB, NG*, P* ports, PUB*, chord tracking (TP*, CP*, TM*, MD1, WC2, EV*, AG*, UP*, PD*, RL*, LU3), TS8*/TU8*, TICK/JUD1/JUD2/AUD1 | stub (hot or custom ABI) |
| cold or diagnostic, same shape as converted ones, not converted in this pass | AT, LT1, DS1, D5, AL1 (tail jump into a C wrapper) | stub (per-chord load loop; candidates for the same pattern) |
