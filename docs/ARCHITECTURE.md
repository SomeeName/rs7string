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
| 3 | [The string-count model (C-lite)](#3-the-string-count-model-c-lite) | 10 | [Plain (unencrypted) PSARC loading](#10-plain-unencrypted-psarc-loading) |
| 4 | [Native capacity-8 layouts](#4-native-capacity-8-layouts) | 11 | [Window, input, overlay, logging](#11-window-input-overlay-logging) |
| 5 | [Rendering the seventh string](#5-rendering-the-seventh-string) | 12 | [Virtual guitarist](#12-virtual-guitarist-test-mode-test-vguitar1) |
| 6 | [Detection and scoring](#6-detection-and-scoring) | 13 | [Eight strings](#13-eight-strings) |
| 7 | [Chords with the 7th string](#7-chords-with-the-7th-string-native-n-slot) | 14 | [Known limitations and open risks](#14-known-limitations-and-open-risks-10) |
| | | 15 | [Mid-hook contract and cold-site conversion](#15-mid-hook-contract-and-cold-site-conversion) |
| | | 16 | [Source layout](#16-source-layout) |

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

**Stubs** (`src/features/<name>/hooks_stubs*.cpp`) are naked functions that replay the displaced stock
instructions. They are checked against the stock code in the Unicorn emulator
(`tools/tests/test_emu*.py`).

**Mid-hooks** (`src/platform/midhook.*`, CONFIRMED by unit and emulator tests) give new sites a plain
C++ callback `void __cdecl Callback(Rs7Mid::Context&)` instead of hand-written assembly. One common
thunk (`src/platform/midhook_thunks.asm`) saves GPRs, EFLAGS and x87/SSE state, clears DF, calls C++, restores, and returns into a
relocated copy of the displaced instructions, which jumps back to `site + stolen length`.
`ctx.esp` is the original game ESP; registers and flags are mutable; assigning `ctx.jump` skips the
replay. The relocator whitelists common ALU/MOV/LEA/TEST, register PUSH/POP, immediates,
MOVZX/MOVSX/SETcc and relative CALL/JMP/Jcc; it rejects prefixes, LOOP/JCXZ, indirect transfers,
unknown opcodes, branches into instruction interiors, internal calls and call/pop. There is no
blind-copy fallback. Callees that inspect their return address need a hand-written hook (contract,
conversion method and status: section 15). Cost
measured on one machine: about 150 ns per call (microbenchmark, not frame time); the light mode
(`Rs7Hook::midMode = Rs7Mid::Mode::Light`, no floating-point save, for callbacks without x87/SSE code) costs
about 25 ns, and the sse mode (`Rs7Mid::Mode::Sse`, XMM0-7 and MXCSR only, for callbacks with SSE float math and no
x87) about 20-31 ns (CONFIRMED, `tests/test_midhook_bench.cpp`, five runs). Besides `jmp` sites a mid-hook can sit
on a `call rel32` (section 15 "Call sites"): **Before** (callback at the callee's entry; `Rs7Mid::Ret` / `Return` replace the callee), **After** (callback after the callee returned) and
**Wrap** (`Rs7Mid::Call::Wrap`: the callee runs on copies of its arguments between an optional before and an after
callback that both see the originals, section 15 "Wrap placement"); `jmp_atomic8` sites take a mid-hook too, and
`Context::St0` / `PushSt0` edit the saved x87 stack. Live detach is not supported. D2/D3 in `features/diag/hooks.cpp` show the registration pattern. To
add one: add a `jmp` site (5..16 bytes, whole instructions), implement the callback, set
`Rs7Hook::callback`, add an emulator comparison, run the gates.

**Copied game functions.** Some stock functions are copied byte for byte from the game image with
only array displacements and count bytes moved to the capacity-8 layouts (`tools/gen_fncopy.py`,
`gen_tuner8s.py`, `gen_publish*.py` -> `src/features/<name>/*generated.h`). The copies are checked against the
live code at startup (FNV-1a) and in the emulator against the stock function. A copy's call into rs7string (a call edit
in place of one stock instruction or call) goes to a call-in entry, a C++ callback with the copy's registers
(`Rs7Mid::CallIn`, section 15 "Call-in entries"), or to one of the few asm helpers left.

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
| Colour manager | `g_rs7_native_colours` | `*0x135F58C`: original six-slot tables retained for RSMods; appended eight-slot tables (both colour modes). |
| FretGrid | `g_rs7_native_fretgrid` | cells of 264h per row instead of 1CCh; hooks FGR*, PUBGC/GO/GB/GA/GF/EDGE/GP. Answers `Rnd_FretGrid_GetCellPos` `0x58FE00` from native cells for strings 0-7. |
| Pitch | `g_rs7_native_pitch` | detector grown to `13E0h` (int16 tuning8 `+13B8h`, openMidi8 `+13C8h`, active count `+13D8h`), matcher to `6Ch`; `0x873E90` replaced by a port with an 8-entry base table (35 / 30 for strings 6 / 7); strings >= 6 of `0x873F80` calls of a tailed object are answered over the tail (PC1-PC4, `rs7p::ExpectedPitch8`). Tails are written only in blocks `_msize` reports large enough; the writers record each such object in a registry of tailed allocations (128 entries; CONFIRMED peak in game: 2 objects, 0 evictions; least recently written evicted and logged once; a writer that finds a stock-size block drops a stale entry), which `Rs7Pitch_Tailed` reads, so the hot readers call no CRT function and stay light-safe. |
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

**RSMods colour compatibility (CONFIRMED by source and native regression tests).** RSMods
`GetStringColor` reads `*0x135F58C`, player mode at `+0x348`, then uses mode stride `0xA8`
and six-column state offsets starting at `+0x350`. Widening those tables in place redirects its
custom-colour, Extended Range and rainbow writes to other states/modes. The manager now keeps
the stock RGB-pointer/UV ABI at `+0x350` / `+0x890` and appends eight-column RGB-pointer/UV tables
at `+0xDD0` / `+0x14D0` (allocation and sized free `0x1BD0`). CMINIT/CMBUILD and palette publication
mirror the first six pointer entries of every state and mode into the stock tables. Both views
alias the same source RGB storage, so RSMods writes immediately reach the corresponding game
colour, including colourblind mode, named peg colours and fret-number states. The extra strings
remain owned by rs7string. Game readers/writers use the appended tables. In-game confirmation
with the reported custom/rainbow settings and 3+3 headstock is still required.

**Selected peg refresh (CONFIRMED by game code and native tests).** `0x59BD70` / `0x59BF80`
copy a named colour into each PegSet slot at `+0x10`; palette edits do not invalidate it.
RSMods deliberately delays its tuner palette application. N5 now refreshes these copies on the
game thread for both six- and seven-string guitars, using the last PSN1 session's player index.
Names 0/1 retain reset/success states 6/5; string names use state 4. The seventh peg recalculates
its brightness from the current stock state-4/state-1 ratio instead of a previously published
extra-colour snapshot. Taken flags, named indices and animation scalars at `+0x20/+0x24` stay intact.
`0x543150` also writes this copied RGB times `ClipFade*2` into `StringColor` before the
`0x543910` glow blur, so stale RGB affects both the peg and its coloured bloom. The black quads
at `0x544610` are a separate path. **HYPOTHESIS:** the RGB/intensity mismatch explains the
reported difference in glow width; verify the appearance in the same tuner stage in game.
The pass doubles that RGB, so `NamedColour` caps the seventh peg's brightest channel at 0.5 (the
peak of the stock named colours logged in game): with RSMods' 00C68E and a state-4/state-1 ratio near 1
the doubled colour exceeded 1 in two channels and the glow turned cyan (HYPOTHESIS: clamped by an 8-bit
target; the ×2 is CONFIRMED in `0x543150`, the target format is not).

**RSMods seventh-string default (CONFIRMED by RSMods source and native tests).** At startup,
inspect the game directory's XInput proxy as resource data, identify its RSMods version resource, and read
the adjacent `RSMods.ini`. This works before RSMods loads and avoids the same-named system XInput DLL.
With `ExtendedRange=on`, use `string0_CB` for custom-colour mode 2, or RSMods' default ER low B
`00C68E` for modes 0/1. Apply it to both profile colour modes. Missing/invalid configuration falls
back to rs7string's own colours. `[color] rsmods_extended_range=0` explicitly selects those saved
colours; toggling the preference never overwrites them. RSMods configuration changes need a restart.

**RSMods pure red glow (CONFIRMED by RSMods source `CollectColors.cpp` and emulator tests; not yet seen
in game).** RSMods' custom string colours write each string's Glow every tick as HSL(hue - 4, 1, 0.5) of
its Enabled colour (int hue from its `RGB2HSL`). For a hue of 0..3 (e.g. `800000`) the hue is negative,
its `HSL2RGB` matches no sextant and keeps a stale near-black colour, so the string's glow outline is
invisible. rs7string corrects the read side, not RSMods' table (RSMods rewrites it every tick):
`Rnd_StringRenderer_UpdateGlow` `0x5441A0` loads the glow RGB pointer at `0x544384` and copies it at
`0x54438B`. GLW (mid-hook at `0x54438B`, stock function) and `Rs7_CallIn_GlowColour` (the GSGL copy's call-in)
replace the pointer with `Rs7_GlowFixPtr`'s answer: for strings 0-5 whose Enabled (state 1, same mode,
stock or native layout) has an RSMods hue of 0..3, a buffer with HSL((hue - 4 + 360) % 360, 1, 0.5),
RSMods' own conversions float for float (`src/features/colours/glow.cpp`); any other colour keeps the pointer.
Only with `g_rs7_glow_fix`: the XInput proxy is RSMods and its `RSMods.ini` has `CustomStringColors=2`
without `UseExactColors=on` (read once at startup), so stock and Zag colours are never touched. GLW is
its own failure group (Glow): a failed site turns only the correction off.

**Tuner menu entry (CONFIRMED by live log and generated-copy emulator).** TS8M2E at `0x5B0135`
normalizes a menu start (submode 0/string 0) to `order[0]`. It must update both EDI and the string
argument at `[ebp+0xC]`: entering from old mode 5 reloads that argument at `0x5B020E` and selects
again at `0x5B0213`. Updating EDI alone selects low B and then E while the stored current string
still says low B. The generated port now preserves the normalized argument for that second call;
fallback submode 1 and explicit reverse-pass starts retain their requested string.

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
mip levels the colour is pure only on mips 0-1.

**RSMods note ramps** (CONFIRMED by RSMods source `D3DHooks.cpp` / `Settings.cpp` and the in-game
log): with custom string colours or Extended Range, RSMods' draw hooks bind their own generated ramp
(256x128 A8R8G8B8 MANAGED, rows 6/7 and 14/15 fixed greys) on stage 1 after the game bound the stock
one, so string 6 at V6 sampled RSMods' grey row 7 / 15. When the installed `xinput1_3.dll` identifies as
RSMods (`rs7rsmods::Present`) and V6 is on, the SetTexture hook gives each such texture (stage 1,
256x128 A8R8G8B8 MANAGED, rows 7 and 15 grey) its own copy with the V6 lines repainted, with the same
build and self-check (any failure: RSMods' texture stays bound), hides it from `GetTexture` and holds a
reference on the source (a rebuilt RSMods texture is a new address; at most 8 cached). A colour change
rebuilds them at the next bind. Limit: more than 32 distinct RSMods ramps (e.g. rainbow notes) turn this
path off for the session; string 6 then shows RSMods' row again.

**Ramp swap source files** (`src/d3d/`, public interface `swap.h`): `swap_hook.cpp` (vtable slots 64/65, thunks,
re-check, GetTexture hiding; `HookState`), `swap_ramp.cpp` (candidate processing, recolour, retire, overlay
status; `RampState`), `swap_foreign.cpp` (the RSMods ramps above), `swap_texio.cpp` (level reads, copy build
with self-check, write with read-back), `hookutil.cpp` (log and module-name helpers shared with `present.cpp`).
Tests: `tests/test_d3d_hook.cpp`, `test_d3d_ramp.cpp`, `test_d3d_foreign.cpp` on `tests/swap_fixture.h`.

**Colour source files** (`src/features/colours/`, public interface `colours.h`; the D3D side above includes only
that): logic `rgb.cpp` (colour text, channels, ramp-line recolour), `native.cpp` (the eight-column layout and
`Publish`), `ramp.cpp` (the ramp copy model), `rampstate.cpp` (candidate table, V6 rule), `rsmods.cpp` (RSMods
proxy and `RSMods.ini`), `glow.cpp` (RSMods' glow conversions); glue `hooks.cpp` (CMUV / CMIX / CMFALL, GLW,
`Rs7_GlowFixPtr`, `Rs7Colour_Start` / `Rs7Colour_CheckManager`), `hooks_publish.cpp` (CMINIT / CMBUILD and which
thread may write the manager); `state.h` (one `State`); `registration.cpp` (`kRs7ColourHooks` with the generated
`colour_hooks.generated.h` from `tools/gen_colournative.py`, group Colours; `kRs7GlowHooks`, group Glow). Tests:
`tests/colours/`. `settings.cpp` holds the `[color]` settings table and its startup read (`Rs7Colour_ReadIni`); `LoadConfig` in
startup still logs the values. `g_rs7_v6`, `g_rs7_fx_rgb` (`native.cpp`) and
the data references X_CM, X_TCS, X_THEAD (`kRs7ColourRefHooks`, group Core) are the feature's too; the CM / GLW write
conditions stay in `Rs7_ShouldPatch` (`src/hook_rules.cpp`) with the other write conditions.

**7th string line** (`string7_line`, `src/features/strings/`, per-guitar state in `src/features/string7/`): string 6
is a stock 0x110 string object of the native GuitarStrings (slot 6 of its capacity-8 arrays, ghost included),
driven by the stock string loops; atlas row V = `[neck] string7_v` (default 0.4 = string 0's row; 0.0-0.3 are
the bass rows). The game's own section dimming lights it (SNG section string mask bit 6: N12 `0x55CC71` / N13
`0x55CE3A`); bends N18-N23 (chart bend on the ghost string `ghost_up_NN`, detected bend `bendup_NN` through
`Rnd_GuitarString_SetBend` `0x598910`) and the chord pluck PL1 reach the native string 6. The stage-1
DLL-owned string object S7 (with its own placement, colour, glow, noteway draw-list fix and GPU diagnostic:
N2-N4, N6, N7, N10, N11, N14-N17) was removed in 1.0. Limit: after `Rnd_GuitarStrings_ResetState` `0x59AC20`
(restart/seek) a running ghost bend is only released by the next string-6 note end.

**Fret bars** (`src/features/frets/`, F1 `0x58FB20`): on a guitar built with 7/8 strings every bar (and
the nut) moves half a string spacing towards string 6 and is scaled uniformly by
`k = (L + d/2)/L` with `[neck] fret_halflen` L (4.61, the mesh's half length); measured d 1.6079,
k 1.1744. Bars are about 17 % thicker. Locators and span data are never written. Chord frames
(B1 `0x7A2BCA`, B2 `0x7A91E4`) get a string-axis scale and shift from the guitar's lane positions.

**Headstock model** (`src/features/headstock/assetload*`, `hsbuild.*`, `[assets] headstock7`): the guitar and its
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
- **Layouts** (`kLayouts` in `assetload_paths.cpp`): "6 in Reihe" (`guitar_standard_inline`, stock part
  `Wilshire_RH/LH`, pegs `epi_whilshire_peg.nif`) and "3+3" (`guitar_standard/gib_lespaul*.nif`, pegs
  `peg_bakelite.nif`). With 3+3 the in-line model sits correctly on the Les Paul neck; the pegs
  follow the profile option, not the frame, so the mod swaps in the in-line peg model
  (`Rs7HeadstockRead_InlinePeg`), and `UI_PlayerTuner_PlacePegLetters` `0x5B1A10` would turn letters
  of pegs >= count/2 round when the InstrumentInfo in-line byte (`+0Ah`, from profile `+13h`;
  `+8` = InvertGuitarStrings, `+9` = LeftHandGuitar) is 0, so LT1 `0x5B1B53` (and the same edit in
  the native tuner's copy) reports such a guitar as in-line (CONFIRMED in game).
- **Peg and knob tint.** The 7th peg is slot 6 of the native PegSet, so the stock peg highlight pass
  `0x543150` tints its knob like the others (the stage-2 mirrored peg and its PH1/PH2 tint hooks were
  removed in 1.0). The pass draws the mesh list `Rnd_StringRenderer_Setup` `0x541880` collected per
  peg clone with NiNode `0x9E1360`, which ignores the cull flag, and never checks the flag itself
  (CONFIRMED by disassembly): every 3+3 clone carries both the bakelite knob and `rs7_alt`, so the
  hidden knob got the glow too (the wider bakelite shape). PGH1 `0x5431DC` skips a mesh whose
  culled ancestor is `rs7_alt` or one of its siblings (`Rs7Asset_PegMeshHidden`).
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

`src/features/nslot/` (records, matcher port, tracker aggregation, the RS7X store and their stubs); the chord frame span (B1/B2), the old
M4 codec and `[chords] native_chordloops` (CL* sites) are `src/features/chords/`. The game's own chord
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

**RS7X extension block** (parser `src/model/extblock.*`, per-song store and site XB1 in `src/features/nslot/`; format: [RS7X-SPEC](release/RS7X-SPEC.md)).
Strings beyond the six stock slots live in a block appended to the SNG body after MetaData, inside the
zlib stream. The stock loader never reads it (CONFIRMED: `SNG_Song_Load` `0x558BD0` only checks the
inflated size against the container's size field and returns after MetaData without comparing the
stream position). XB1 hands the stream to `Rs7Ext_OnSngLoaded`, which validates it against the loaded
song and keeps it per song; the N-slot records are built from it. An open extended string gets a
ChordNotes record with its OPEN bit like an open stock string; a chord of one stock string plus one
extended string is a double stop.

## 8. Tuner and tuning names

**Native 8-slot tuner** (mandatory since 1.0, `src/features/tuner/`, `gen_tuner8s.py`; the six-slot B-phase tuner was removed). Sessions are
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

**Low range** (`[tuner] lowrange=1`, default on): the stock "wrong instrument" check
(`UI_PlayerTuner_CheckWrongInstrument`, site W `0x5B2DDE`, `fcomp float [34.0]`) uses a session
threshold `min(34, lowest open MIDI - 1)`; the detector's lowest-open filter (`+11F8h`) is lowered on
each selection of a 7-string session. A bass B0 (MIDI 23) is not handled.

**Tuner arrow keys** switch strings only in the precision view (a played note or Space brings it up);
the stock 6-string tuner behaves the same.

**Tuning names** (`[tuner] names=1`, `src/features/tuner/tuningname.*`, `hooks_names.cpp`). For an arrangement whose `Tuning` has
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

Source: `src/features/diagrams/` (public interface `diagrams.h`).

The chord book, song panel and lesson scene draw chord diagrams from Scaleform movies
(`gfxassets/libs/commonlib.gfx`, `gfxassets/views/contentpanel.gfx`, `gfxassets/views/ge_game.gfx`).
Seven-row diagrams need a changed ActionScript class (DoInitAction tag, type 59, class sprites
1103 / 28 / 26) and extra data keys.

- **Data delivery.** DGM `0x757698` (tail of `0x757030`) and DGS `0x7580DF` (tail of `0x757960`) add
  `LowBFret/LowB_Finger/LowB_Color` and `LowFsFret/LowFs_Finger/LowFs_Color` (colours `0x808080` and a
  provisional `0x00FFFF`) from the native T8 record after the stock serializers have run, recomputing the
  fret window over N strings. TP13 `0x7578BC` routes the loaded-song template lookup through
  `Rs7Ns_TplRecord`; DGB `0x55A02C` binds PostProcess's UUID-selected attributes to the exact SNG song.
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

`[psarc] plain=1` (`src/features/psarc/`, the first feature in the target layout, section 16; sites group `psarc`). Three sites, CONFIRMED by emulator and in
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

**UI state** (`src/features/testdrive/uistate.*`, always on, log only): a thread polls the game's current UI state
every 100 ms and logs `ui: OLD -> NEW` (`MainMenu`, `LearnASong_Songs`, `LearnASong_SongOptions`,
`LearnASong_PreSongTuner`, `LearnASong_Game`, `LearnASong_Pause`, `LearnASong_SongReview`, `Tuner`, ...).
The state lives at global `0x135F62C` +28h, its name at +8Ch (CONFIRMED static: `0x766580` compares it
with `_stricmp`; in-game log 2026-10-17). In-game test drivers wait on these lines instead of fixed delays.
A screen's state is logged when it opens, before it takes keys (about 1.5 s of intro animation).

**Test speed-up** (`src/features/testdrive/fastmode.*`, `[test] fast=1`, default off; sites.json group `fast`, resolved and
written on their own after plain PSARC): `FST_REVIEW` `0x623BC1` `jz` -> `nop nop` in the after-song menu pick
`0x623B60`, so a song ends on `LearnASong_SongOptions`, not `LearnASong_SongReview` (CONFIRMED branch and in-game
run, about 19 s saved per song; whether the review screen saves anything the profile needs: HYPOTHESIS, not checked).
Tried and dropped (measured 2026-10-17): a faster Flash clock (Scaleform `TimerOverride*` `0x133B19C`) or Flash
timeline (dt in the advance wrapper `0xC29E10`) does not shorten the menus' ~1.5 s key lock or the boot.

**Test variants** (`src/features/testdrive/variants.cpp`, `hooks_variants.cpp`; `[test] force_colourblind=1`,
`force_lefty=1`, `force_headstock=33|6`, default off; TVCB / TVGP written only when one is on). The profile's settings
store holds `ColorBlindMode`, `LeftHandGuitar` and `GuitarTypeMP` (`6-IN-LINE` or not); the game copies them into
runtime objects, and only those copies get the forced values (CONFIRMED static):
- TVCB `0x519687` in `Rnd_ColorMgr_SetColorBlindMode` `0x519680`: EBX (a temporary copy of the setting text, from
  0x519750 or 0x601820) becomes `ColorBlindMild`, so the colour manager's mode (`+348h + 4 * player`) is 1.
- TVGP `0x593D4A` in `Rnd_GuitarProfile_Load` `0x593B00` (ESI the runtime GuitarProfile): `+11h` lefty, `+13h`
  in-line (AL before its store) and `+15h` colourblind. `Rnd_InstrumentInfo_FromProfile` `0x58ABD0` copies +11h / +13h
  into InstrumentInfo `+9` / `+0Ah`, which the headstock frame choice and the assetload log read.

Nothing writes the store back from these copies: its setters run from the options menus (for example `0x891050`
sets `LeftHandGuitar` from the handedness option's text), so the owner's profile keeps its values. Not covered: UI
code that reads `LeftHandGuitar` from the store itself (for example the chord-diagram colours `0x75812F`).

## 12. Virtual guitarist (test mode, `[test] vguitar=1`)

`src/features/vguit/` (section 16: logic in `config`, `plan`, `report`, `player`, `synth`; game glue in
`hooks*.cpp`; tests in `tests/vguit/`). Plays the chart into the game's own input path with a synthesized
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

Design record of the generic mid-hook (`src/platform/midhook.*`) and of converting hand-written naked
stubs to it. Tags as above; measurements are one machine and one microbenchmark.

**Source.** The per-site entries are emitted at runtime by `Rs7Mid_Build`, `Rs7Mid_BuildLight` and `Rs7Mid_BuildCall`
(`midhook.cpp`). The two shared thunks they jump to, `Rs7Mid_Thunk` and `Rs7Mid_ThunkLight`, are one MASM file,
`src/platform/midhook_thunks.asm` (ml.exe, wired into `rs7string.vcxproj` and `tests/rs7string_tests.vcxproj`
through the `masm` build customisation, exported with `PROC EXPORT`), not inline `__asm`; the WOW64 syscall of
`Rs7_Protect` is `src/platform/protect_syscall.asm` the same way, so `src/platform/` has no inline assembly. Why a
MASM file: the thunks stay ordinary code in the image's `.text` with the same exports, so the native tests, the
emulator tests (which load the DLL and run the exported thunks in Unicorn) and the benchmark use them unchanged, and
the assembler reads like the old inline block with labels, comments and a `FOR` macro for the light thunk's unrolled
copy. Emitting the thunk bodies as bytes at runtime would need executable storage and a byte emitter for about 70
instructions, hide them from the export table and the emulator harness, and buy nothing in speed. The move changed no
byte: CONFIRMED, both thunks in `dinput8.dll` are byte-identical to the inline-asm build (`f7cb7e8`, 92 and 159
bytes), and the benchmark below is unchanged (full 150-157 ns, light 21-25 ns before and after, three runs each).

**Contract.** `Rs7Mid::Context` is 44 bytes: EDI, ESI, EBP, original ESP, EBX, EDX, ECX, EAX, EFLAGS
(nine dwords), then the thunk-owned `fpu` slot (the callback address on entry; while the callback runs the FXSAVE
image in full mode, 0 in light mode, the result block with bit 0 set in sse mode) and the writable `jump`. Compile-time offset assertions
bind it to the thunk. GPRs, ESP, flags and `jump` are writable; the callback is `cdecl`, must return
normally and must not throw. Segments are the process ABI. In full mode x87/MMX/SSE/MXCSR are saved with FXSAVE
and restored with FXRSTOR, so a callback changes the resumed floating-point state only through the x87 accessors
(below); light mode saves no floating-point state, sse mode only XMM0-7 and MXCSR (their own paragraphs below; AVX
upper halves and privileged state are outside the game's ABI). The generated 15-byte entry pushes the replay
address and the callback, then jumps to the shared thunk; the full thunk saves GPRs and flags, copies the context,
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

**Relocator.** Whitelist only (see section 2), a table of position-independent operand forms (ModRM, imm8,
operand-sized immediate, moffs32, allowed ModRM.reg values) plus the relative transfers: accepts one
operand-size prefix `66` except on transfers, x87 and `SETcc`; rejects every other prefix,
indirect transfers, unknown opcodes, branches into instruction interiors and internal calls; widens
relative branches and remaps internal ones; no blind-copy fallback. `tools/midhook_survey.py` runs the
built relocator over every jmp site and lists the rejected ones with the first rejected encoding: 22 of
210 before `INC`/`DEC r/m8`, `IMUL` (`0F AF`, `6B`, `69`), `TEST r/m,imm` (`F6`/`F7 /0`) and `66` were added,
2 after (W: `fcomp`, BT1: `RET imm16`); `RET imm16` is now copied as a terminal instruction like `RET` (position
independent, only as the last displaced instruction), and FCOMP m32 (`D8 /3`, memory forms only: a memory operand is
position independent in 32-bit code) is accepted, so none is left. CONFIRMED by `tools/tests/test_midhook_relocate.py`: the relocated
copies of 14 of those sites execute like the stock bytes in Unicorn (GPRs, EFLAGS, exit, writes). `tools/sitecheck.py` still checks whole instructions and inbound
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
hand-written until profiling justifies full-context overhead (or convert to the light mode below once its callback is integer-only). Wrappers that call the original and do work
around it use the Wrap placement (below). Also stay hand-written: callee `RET imm` cleanups and epilogues, replaced `CALL` sites
that tail-jump, sites whose displaced bytes use a prefix other than `66` or INT3 padding, custom saved-frame consumers,
stack probes and return-address-sensitive callees (table below). A converted site either leaves the
replay alone (the callback only observes or calls a helper) or skips it (`jump = resume`) and performs the
stock instruction's effect itself; where the old stub left compare flags, the callback reproduces them
(`Context::Compare`).

**Light mode.** `Rs7Hook::midMode = Rs7Mid::Mode::Light` (`Rs7Mid_BuildLight`) uses the same 15-byte entry,
`Context` layout and continuation rules, so converting a stub is only writing the callback. Its thunk
(`Rs7Mid_ThunkLight`) saves GPRs and EFLAGS, places the context 44 bytes below the entry slots as the full
thunk does (a callback may still `Push` up to eleven dwords), clears DF, calls C++ and writes the edited frame
back with unrolled moves (no REP MOVSD, no STD). It does no FXSAVE, FNINIT or FXRSTOR and no 16-byte stack
alignment. Contract: the callback and everything it calls execute no x87, MMX or SSE instruction (they would
change the game's live floating-point state). In MSVC with `/arch:SSE2` that excludes not only `float`/`double`
but also zero-initialised structs and arrays and struct copies, which the compiler emits as `xorps`/`movups`/`movq`
(several existing cold callbacks contain exactly that). Enforcement: a site listed in `LIGHT_CALLBACKS`
(`tools/tests/emu_harness.py`, kept in sync with the registration) is built with the light builder in the
emulator tests, and `test_light_callbacks_use_no_fp` disassembles its callback and every direct callee
(recursive descent) and fails on any x87/MMX/SSE instruction or unverifiable indirect jump. CONFIRMED (emulator,
`test_light_thunk_executes_no_fp_and_keeps_fp_state`): with CR0.TS set, so that any floating-point instruction
would raise #NM, the light thunk runs to the continuation, preserves all GPRs and EFLAGS and leaves the FXSAVE
image unchanged; the full thunk faults under the same setting. The generic emulator tests (real-site replay,
relocated JCC/CALL, register/flag/ESP/jump edits) run against both thunks.

**Sse mode.** `Rs7Hook::midMode = Rs7Mid::Mode::Sse` (`Rs7Mid_BuildSse`; mode 2 for `Rs7Mid_BuildCall` /
`Rs7Mid_BuildWrap`) is for hot sites whose callback computes with floats: the light thunk's frame handling plus
XMM0-7 (MOVUPS, unaligned) and MXCSR (STMXCSR / LDMXCSR) around the callback, which runs with MXCSR `1F80h` (round to
nearest, exceptions masked, no FTZ / DAZ) whatever the game set. No FXSAVE, no FNINIT: the game's x87 stack stays
live, so the callback and its callees execute no x87 or MMX instruction. With MSVC `/arch:SSE2` that rules out a
non-inlined function returning `float` / `double` (the x86 ABI returns them in ST0; `Rs7S7_ExtrapolateLane` is inline
for this) and CRT math. An x87 result goes through `Rs7Mid::LoadSt0(c, value)`: `Context::fpu` points at a result block
(`Rs7Mid::SseResult`, bit 0 set; the x87 accessors see no image and fail), and after restoring the thunk does
`fld dword` of the value, as a displaced `fld dword [..]` would (one load per hit; a full stack faults like the game's
own `fld`). The gap below the entry slots is 52 bytes, so the block and the context tail stay below the eleven dwords
a callback may `Push`. Enforcement: `SSE_CALLBACKS` (`tools/tests/emu_harness.py`) selects the builder in the emulator
tests, and `test_sse_callbacks_use_no_x87_or_mmx` runs the light check's scan allowing XMM instructions, with no
exemption: R2's D1 logger `Rs7_DiagNoteHead` only queues the line (integer, a lock-free 16-slot ring) and the full-mode
diagnostic hooks D2 D3 D5, written under the same `[debug] diagnostics=1`, log it (`Rs7Diag_FlushNoteHeads`). CONFIRMED:
`tests/test_midhook_sse.cpp` (hardware: the loaded value, XMM / MXCSR / x87 state around the hook, `LoadSt0` fails in
the other modes), `test_midhook_emu.py` (Unicorn: registers, flags, ESP edits and real-site replay against all three
thunks; the callback sees MXCSR `1F80h` and the game's x87 stack). Cost: see the table under "Cost". `Context::Add32`
(ADD's flags, like `Compare`) and `Context::CompareSt0` (the exact order of the 80-bit ST0 against a float, as `fcom`;
`St0()` rounds to double) were added for R2 / R6 and W; both are checked against the hardware.

What the converted float sites guarantee (`tools/tests/test_midhook_sse_sites.py`, against the frozen stubs and the
stock bytes): R2 and R6 run to the game's next `fstp dword` (R2 `0x79E730`, R6 `0x7A8C30`, CONFIRMED by disassembly),
and the stored float, registers, flags (the `add`'s, `Add32`) and memory equal the old stubs' for exact, inexact,
overflowing and signed-zero lanes; the extrapolation is correctly rounded once (53 bits >= 2 x 24 + 2), as the old
x87 code's float was. Not covered: the x87 precision / underflow / denormal flags (contract `fsw="sticky"`, and no
denormal lane coordinates in the matrix: an x87 load of one raises DE), NaN payloads (x87 and SSE propagate different
NaNs when both inputs are NaN), and the game's x87 control word: the oracle runs with the FNINIT default (64-bit
precision, round to nearest); HYPOTHESIS: the game runs round to nearest at 24-bit or higher precision, where the
stored float is the same. W compares the 80-bit mean exactly (a mean one 64-bit ulp off 34.0 included); its replay
and the remap read the game's constant k at `0x1224C6C` (its address from the ref site `X_WTHR`, the operand of W's
`fcomp`), CONFIRMED 34.0 in the unpacked image; ST0 becomes k - 1, k or k + 1 (the old stub used its own 34.0f,
`g_rs7_wrong_stock`, retired). The sse thunk on a full x87 stack loads like the game's `fld` (stack fault, compared
with the hardware), and `BuildCall` / `BuildWrap` run with mode 2 in their emulator tests.

**Call sites.** A `Rs7Kind::Call` site is a `call rel32` whose rel32 startup retargets to the hook's entry
(`Rs7Hook::callback` on a call site, `Rs7Mid::Hook::PrepareCall`, `Rs7Mid_BuildCall`). Nothing is relocated, and the
game's own `call` pushes the real return address. Same thunks, `Context`, full/light rules and flag contract as jump
sites; only the entry and the meaning of `jump` differ, chosen per hook by `Rs7Hook::midCall`:

| Placement | Entry | When the callback runs | `jump` starts at |
|---|---|---|---|
| `Rs7Mid::Call::Before` (default) | `push callee; push callback; jmp thunk` (15 bytes) | at the callee's entry: `esp` points at the return address (site + 5), arguments at `esp + 4` | the original callee |
| `Rs7Mid::Call::After` | `lea esp,[esp+4]` (drops the game's return address, keeps the flags); `call callee; push site+5; push callback; jmp thunk` (24 bytes) | after the callee returned: EAX/EDX/flags are the callee's, `esp` is past its own `ret N` cleanup | site + 5 |

Before: leaving `jump` tail-jumps into the callee with the game's stack and return address unchanged (what the old
`jmp g_rs7_target_X` stubs did); another `jump` replaces the callee (same convention); `Rs7Mid::Return(c, value,
argBytes)` skips it: EAX = value, the return address and `argBytes` of a `ret N` callee are popped, and execution
continues at site + 5. After: the callback may replace the integer return value (EAX, EDX); the callee sees a return
address inside the entry, so return-address-sensitive callees (table above) cannot use it, as with a relocated
`CALL`. An x87 return value (ST0) can be replaced by a full-mode callback (`Context::SetSt0`, below), not by a light
one. The callee address is the first entry slot (Before) or bound to the hook's `target`
variable (both). After cannot read the pre-call arguments (they are below the live stack after the callee returned and
the thunk's scratch overwrites them); wrappers that need them use the Wrap placement below.
CONFIRMED by `tests/test_midhook_call.cpp` (native: registers, flags, stack, call / skip / `ret N` skip / replaced
return value, both placements and modes) and `tools/tests/test_midhook_call_emu.py` (the same in Unicorn against both
thunks). First converted site: SF1 (Before, full mode: the song destructor runs on a loader thread too), compared
with its frozen stub in `tools/tests/test_midhook_nslot.py`.

**Call-in entries.** The calls written into relocated function copies (platform/fncopy.h; a call edit replaces one
stock instruction or call with `call` + NOPs) are not sites: nothing is relocated and there is no callee. A
`Rs7Mid::CallIn {callback, mode, argBytes}` builds the call entry with no callee (`Rs7Mid_BuildCallIn`, 16 or 18
bytes): `push continuation; push callback; jmp thunk`, the continuation a `ret` / `ret argBytes` right after it. The
callback runs with the copy's registers and flags, `esp` at the copy's return address, arguments at `esp + 4`.
Leaving `jump` returns to the copy with the edited `Context` (registers, flags) and pops `argBytes` (a stdcall helper's
`ret N`); assigning it tail-jumps (into the stock callee, with the copy's stack: `Rs7_CallIn_Attach` for strings 0-5).
A callback that must run a game function and continue afterwards pushes copies of its arguments and a second call-in
entry as the return address (`Rs7_CallIn_Shape` / `Rs7_CallIn_ShapeDone`), so the game function runs with the game's
own x87 state. The generators name the call-in object instead of a DLL function (`g_rs7_callin_<name>`, the edit row's
`Rs7FnEdit::callin`); `Rs7Fn_Build` writes `CallIn::Entry()` (built on first use, before the copy) into the copy. Same
thunks, full / light rules and cost budget as sites; a light call-in callback is checked for x87/SSE code like a light
site (`test_light_and_sse_callins_use_no_fp`). An x87 result (ST0 left for the copy) comes from an Sse-mode call-in
with `Rs7Mid::LoadSt0`: the SSE thunk does the `fld` after restoring (`Rs7_CallIn_GlowRead`, the GSGL copy's glow
read; frozen at `06733ba`). **Game code from a full-mode callback** runs under `Rs7Mid::GameFcw` (`midhook.h`): the thunk's FNINIT
control word (37Fh, 64-bit precision) is replaced for the scope by the game's own from the saved image (the D3D9 device
sets 24-bit precision), as under the naked stubs these callbacks replaced. Audit (2026-10-18, every full-mode callback
that reaches a game function through `call` or a function-pointer cast): `Rs7_CallIn_Attach`, AUD1, DGB, DGM, F1, FGHK,
GKT (around the rebuild: the callback uses `__try`), GSC1, IN1, MD1 (before), N5, NG9, PL1, PSL2, PUBGA, PUBGB, SF1,
TJ (both), TK, TN (both), TU, TUT, XB1. CONFIRMED by `midhook_game_fcw_runs_game_code_with_the_callers_control_word`
and the call-in oracle (it runs with the game control word 27Fh and records the one Rs7Gs_AttachExt and the attach
stand-in see). CONFIRMED by `tests/test_midhook_call.cpp` (sizes, rejections,
the `ret N` return with edited registers in both modes) and `tools/tests/test_midhook_callin.py` (`callin_oracle.py`:
each call against its frozen asm helper and the edit's stock bytes in Unicorn).

**Wrap placement.** `Rs7Mid::Call::Wrap` is for the wrappers After cannot express: the callback needs the callee's
arguments or the registers it was called with after it returned, skips it conditionally, or passes it arguments it
builds itself. The entry runs the callee as a subroutine on **copies** of its stack arguments, so the originals stay
live above the game's return address for the whole call; nothing is added to `Context` and the thunks are unchanged.
Per site the hook row declares `Rs7Hook::midWrap`, an `Rs7Mid::Wrap {args, conv, locals}`: the stack argument dwords
to copy, who cleans them up (`Conv::Stdcall`, also for a thiscall callee: the callee's `ret 4 * args`; `Conv::Cdecl`: the
caller) and the bytes of scratch the callbacks may use (a multiple of 4, at most 256). The same constant is the
callbacks' view of the frame. The callbacks are `Rs7Hook::midBefore` (optional) and `Rs7Hook::callback` (after;
optional when there is a before callback), one mode for both. Entry (`Rs7Mid_BuildWrap`), at the game's `call`:

```
push edx; push ecx; push eax; push 0     ; the frame: user dword, then EAX/ECX/EDX at the call
lea  esp, [esp - locals]                 ; scratch (omitted when 0)
[push C1; push before; jmp thunk]        ; before: jump = C1
C1: push dword [esp + locals + 16 + 4 * args]   (args times: the copies, last argument first)
call callee
[lea esp, [esp + 4 * args]]              ; cdecl: drop the copies the callee left
[push T; push after; jmp thunk]          ; after: jump = T
T:  lea  esp, [esp + locals + 16]
    ret  4 * args  |  ret                 ; the cleanup the game expects from the callee
```

Both callbacks see `Context::esp` at the frame, `Rs7Mid::Frame(c, wrap)` (`midhook.h`):

| `c.esp + ...` | Content |
|---|---|
| 0 | `locals` bytes of scratch (`Rs7Mid::Locals(c)`), uninitialised |
| `locals` | `user`: 0 at the call; the before callback may leave a value for the after callback (a handle) |
| `locals + 4 / 8 / 12` | EAX, ECX, EDX as the game called (not the before callback's edits) |
| `locals + 16` | the game's return address (site + 5) |
| `locals + 20 + 4 * i` | argument `i` (`Frame::Arg(i)`): the caller's own slot, which the callee never sees |

Before callback: the registers, flags and arguments the callee gets. It may edit registers (resumed into the callee)
and arguments (copied after it returns, so the callee sees the edits; a built argument may point into `Locals`), set
`user`, or skip the callee: `Rs7Mid::WrapReturn(c, wrap, value)` (EAX = value, frame, return address and the callee's
cleanup popped, continue at the return address). After callback: EAX/EDX/ECX/flags and the x87 stack (full mode:
`St0` / `SetSt0`) are the callee's, ESP is the frame (the callee's copies are gone), the arguments and `user` are
readable; it may replace the return value; `jump` = T. A **jmp site at a function entry** may use the same placement
(`Rs7Mid::Hook::Prepare` with a `Wrap`): the entry's callee is the relocated prologue followed by a jump to site +
length, i.e. the stock function, and T returns from the function. Limits: the callee runs with a return address inside
the entry and its arguments 16 + `locals` + 4 * `args` bytes lower than at the site (return-address-sensitive callees
and callees that read past their declared arguments cannot use it); register arguments are whatever the registers hold
(nothing to declare). An edit of `Frame::Arg(i)` lands in the caller's own slot: after the call that slot is popped
(stdcall) or dead (cdecl: the caller only drops it). Cost: one thunk pass per callback (two with a before callback),
the copies are a few pushes. CONFIRMED by `tests/test_midhook_wrap.cpp` (native: the original arguments after the call
under the callee's scribble, the frame registers, `user`, argument and register edits, skip with both cleanups, a
replaced return value, an argument built in the scratch, a function-entry wrap with its prologue replayed, an x87
result read and replaced in full mode, both modes) and `tools/tests/test_midhook_wrap_emu.py` (the same in Unicorn
against both thunks, the light entry under CR0.TS). Converted: PP1, IN1, F1, PSN2, AM1, TJ, TU, TN (conversion status
below), then MD1 (the oracle's `Spec.clobbers` declares ECX/EDX dead caller-saved registers at its return: the old stub
left the C helper's there). A call site whose old stub was a jmp over game code before the `call` (GSA1 `push 40h; call`)
is compared with `Spec.lead`: the old stub starts at site - lead, the new and the stock runs execute the original bytes
from there with the call retargeted.

**x87 ST0 (full mode).** The full thunk hands the callback its FXSAVE image (`Context::fpu`), and the accessors edit
the x87 stack in it before FXRSTOR: `FpuDepth()` (valid registers), `HasSt0()`, `St0()` (the 80-bit ST0 rounded to
double, round to nearest even; a quiet NaN on an empty stack), `SetSt0(v)` (replace ST0, depth unchanged; fails on an
empty stack) and `PushSt0(v)` (what `fld` does: TOP - 1, the value in ST0, C1 = 0, the tag bit set; fails when all
eight registers are valid, where `fld` would overflow). The image keeps ST(i) in stack order and the abridged tag
byte by physical register, so a push shifts ST0..ST6 down and updates TOP and the tag together; FXRSTOR then loads
a consistent stack. The conversions are integer code (no x87 instruction, no dependence on the callback's control
word): double -> 80 bits is exact (a signalling NaN is quietened, as `fld qword` does), 80 bits -> double rounds as
`fstp qword` does under the FNINIT control word (unnormals give the default NaN). Light mode has no image: `fpu` is 0,
`FpuDepth()` is 0 and every edit fails, and a light callback still must not execute floating-point code (the accessors
take and return `double`). CONFIRMED by `tests/test_midhook_st0.cpp` (native, real entries): round trips of zero,
signed zero, normal, `DBL_MIN`/`DBL_MAX`, the smallest and largest denormal, infinities and NaNs with payload (TOP and
tag unchanged), `SetSt0` bytes equal to the hardware `fld` of the same value, round-to-nearest-even ties and the carry
into the exponent, push / full-stack refusal and the light-mode case. Jump-site callbacks run before the replay, so an
x87 result of the displaced code is produced by skipping the replay and pushing it (`PushSt0`), as R3a-R3c and R4 do
(`features/string7/hooks_lanes.cpp`, string 6: `2 * cell0 - cell1` (`Rs7S7_ExtrapolateLane`) into the scratch vec3,
EAX = the scratch, ST0 = its x, R4 also its `pop esi`). On a full x87 stack `PushSt0` refuses (both edits are
`[[nodiscard]]`); the hook then rebases EAX so that the replayed `lea; fld` loads the scratch, overflow included,
which is what the old stub did, and logs it once. Their float result equals the old x87 code's for finite results (both correctly
rounded: 53 and 64 bits are at least 2 x 24 + 2); the sticky x87 exception flags (precision) the old arithmetic
raised are not reproduced, which the oracle contract `fsw="sticky"` states (Unicorn does not raise the precision
flag, so the emulator cannot show that difference; HYPOTHESIS: the game reads no sticky x87 flag). Cost: HYPOTHESIS
within the budget, they run from `Rnd_NoteFx_Spawn` / `Rnd_NoteFeedback_Spawn` (CONFIRMED call graph), once per
spawned effect, not per frame. R2 (note head z, per note and frame) and R6 (lane offset, `Rnd_GemLaneOffset_Update`,
per gem and frame) also leave an x87 result but are over the full mode's budget: sse mode (below).

**Atomic sites.** A `jmp_atomic8` site is a 5-byte jmp inside one aligned qword, written with one locked exchange
(the code may run on another thread while it is patched). A mid-hook is prepared for it as for a `jmp` site, with the
whole `expect` span as the replay (the jmp ends inside an instruction, which the relocator would reject), and startup
writes the entry with the same locked exchange (`PrepareMid` / `Apply` in `startup.cpp`; one condition more). S3 and
PT1 (the guitar / bass exits `mov [eax+1356h],cx|dx; ret` of `Play_Detector_SetTuning`, cold) are the first: the
callback does the store itself, calls the helper, which sees the finished detector as before, and returns for the
function (`Rs7Mid::Ret`); the old stubs left the helper's flags at the return, where no one reads them (contract
"dead"). Compared with their frozen stubs in `tools/tests/test_midhook_x87.py` (fixture `midhook_318618f.json`, with
R3a-R3c and R4).

**Cost.** Native QPC microbenchmark (`tests/test_midhook_bench.cpp`, dummy `mov eax,7; ret` site, no-op
callback, median of 7 x 200,000 hits, several runs, Ryzen 7 5800X, Release Win32). The rows are the production
thunks and ablated copies of the full thunk:

| Variant | ns per hit | Over stock |
|---|---|---|
| stock code / direct `call` to the callback | 1.3-2.7 / 2.2-3.6 | - / about 1 |
| full thunk (production) | 149-162 | 146-160 |
| full without FNINIT | 123-137 | 122-135 |
| full without FXSAVE / FNINIT / FXRSTOR | 64-72 | 62-70 |
| **light thunk (production)** | **19-26** | **17-25** |
| **sse thunk (production)** | **20-31** | **18-30** |
| PUSHFD / PUSHAD / call / POPAD / POPFD only (lower bound, not a valid thunk) | 18-24 | 15-21 |

Breakdown of the full thunk's about 150 ns: FXSAVE + FXRSTOR about 65 ns, FNINIT about 20 ns, the two REP MOVSD
context copies with STD/CLD about 45 ns, the register save and call about 20 ns. The HYPOTHESIS that most of the
cost is FXSAVE/FXRSTOR is partly CONFIRMED: the floating-point handling is the largest part (about 57 % with
FNINIT, 42 % without), but the string copies are a third of it, which is why the light thunk also unrolls them.
Not frame time, and the callback's own work is extra (a 64-entry table scan for the PegSet sites, a short
loop for GSSS/GSSE). At 10,000 hits per frame the full thunk would cost about 1.5 ms per frame and the light one
about 0.25 ms, hence the cost budget: full mode where hits per frame x 150 ns stays well under about 20 us (per-frame drivers, a few calls per frame, per song or load, once per event); light mode (or asm) for sites with many hits per frame (per note, gem or mesh, audio thread), whose callback must be integer-only. HYPOTHESIS (hit counts not measured in game): the converted sites run a few to a few dozen times
per guitar load, tuner session or song (PSM2 at most 8 per renderer setup), i.e. well under 10 microseconds
per song.

**Remaining stubs.** The few naked stubs left (listed in the maintainer's handover notes) keep these rules, which used to head the
deleted `platform/stub_macros.h`: a stub is a naked function the patcher jumps to from a game address; it replays
the displaced instructions, does its work and jumps back. It is exported as `Rs7_Stub_<ID>` with its link variables
`g_rs7_resume_<ID>` (site + length; the stub ends with `jmp g_rs7_resume_<ID>`) and `g_rs7_target_<ID>` (an
original branch / call target), all plain `extern "C" __declspec(dllexport)` declarations in the stub's file. It
preserves every register and flag the stock code reads afterwards (flags only where the comment says "flags dead"),
leaves the x87 / SSE state alone unless the comment says it is saved, and never names an inline-asm operand like a
register or mnemonic (C4405 is an error). The comment above a stub says where (site, address, function), what the
stock code does and what changes. No new stub where a mid-hook does the job.

**Conversion status.** 25 sites were converted from naked stubs (the SE pilot's 3 and 22 cold sites), then 6 in refactor step 7 batch 1, 5 in batch 2 and 28 in batch 3, SF1 as the first call site (Before placement) and 15 call sites in batch 4, then R3a-R3c and R4 (x87 ST0) and S3 and PT1 (atomic sites), and 81 sites in batch 5, then 8 wrappers with the Wrap placement, 10 sites in the asm-reduction pass 9 in its part 2 (PC1, PC4, PUBZ0-PUBZ6, plus the PUBAN copy's expected-pitch call) and 2 in part 3 (PP_TOC, PP_SNGF); 3 of them (N2, N3, N7) went with the legacy S7 object in 1.0, as did 37 hand-written stubs. "stub" = still hand-written.

| Family | Sites | Status |
|---|---|---|
| song end log | SE1-SE3 | mid-hook (pilot) |
| tuner session / tuning names / SNG load | TK, TS, TUT, XB1 | mid-hook |
| pool request counts, GuitarStrings / InstrumentInfo / PegSet free sizes | N1, N21, GSZ1, GIZ1, PSA2 | mid-hook |
| native PegSet destructor / unload / ctor / load | PSD1, PSD2, PSU3, PSC1, PSL1 | mid-hook |
| native PegSet mesh latch / colour reset / colour name; GuitarStrings dimming at song start / end | PSM2, PSR1, PSN1, GSSS, GSSE | mid-hook |
| relocator rejected the displaced bytes before PR #54 (`66`, `INC`/`DEC r/m8`, `IMUL`, `TEST r/m,imm`); `fcomp` since the sse pass | W | mid-hook, full mode (`features/tuner/hooks.cpp`: the replayed `fcomp [34.0]` on ST0 remapped to 33 / 34 / 35 by `Context::CompareSt0` against the session threshold; BT1 went with `RET imm16`; PSU1, PSU2, PSM1, NG1, PUBEDGE, CLBM, CLBA in batch 5) |
| sse pass (stubs frozen at `7f80b7b`, `tools/tests/fixtures/midhook_7f80b7b.json`, `tools/tests/test_midhook_sse_sites.py`, frozen stubs and stock bytes): string 6's note head z and lane offset y per note / gem and frame (`LoadSt0`); the per-cell position query, the whole function (`Rs7Mid::Ret`) | R2, R6; PUBGP (R1) | mid-hook, sse mode (`features/string7/hooks_lanes.cpp`, `features/publish/hooks_gridsites.cpp`) |
| call-kind sites that stay asm: call game functions with a custom convention (B2), a cdecl C++ function that replaces the callee directly, no stub (PP_SNGD) | PP_SNGD, B2 | stub (reasons in the stubs' header comments) |
| call sites, batch 4: string-mask test (callee replaced, `Rs7Mid::Return`) and eval target arrays (one callback each, light); song tuning, tailed expected pitch of a tracked note / an analysed mistake (full); PegSet load complete (After placement, full) | TM1-TM6, EV1-EV5; S4, PC2, PC3; PSL2 | mid-hook (`features/nslot/hooks_calls.cpp`, `features/pitch/hooks_calls.cpp`, `features/headstock/hooks_mid.cpp`; stubs frozen at `b6071a8`, `tools/tests/fixtures/midhook_b6071a8.json`, `tools/tests/test_midhook_calls.py`) |
| Wrap placement (full mode): wrappers that read the callee's arguments or the registers it was called with after it returned (PP1, IN1, F1), skip it (PSN2), build its arguments in the scratch (AM1), hand a handle from before to after (TJ, TU, TN; within the budget, CONFIRMED in game with a temporary per-second hit counter, 2026-10-18: TJ about 194 calls once per song-list build, then 0 while the list or a song is shown; TU 1 per song start; TN 1 every 2-3 s in the pause tuner and the menu tuner, 0 during a song. Never per frame: the largest burst costs about 60 us once) | PP1, IN1, F1, PSN2, AM1, TJ, TU, TN | mid-hook (`features/nslot/hooks_wrap.cpp`, `features/frets/hooks_wrap.cpp`, `features/headstock/hooks_mid.cpp`, `features/tuner/hooks_wrap.cpp`; stubs frozen at `b72bc52`, `tools/tests/fixtures/midhook_b72bc52.json`, `tools/tests/test_midhook_wrap_sites.py`, which also runs their stock-bytes references) |
| asm-reduction pass (stubs frozen at `b72bc52` / `c15ca8d`, `tools/tests/test_midhook_asm_sites.py`, frozen stubs and stock bytes): the guitar ctor's allocations and GuitarStrings ctor re-sited to their `call` (GSA1 Before: the size argument; GSC1 Wrap: the entry runs the stock ctor with the game's x87 state, Rs7Gs_Construct adds the native layout after it; GIA1 Wrap: size before, extension after), MD1 (Wrap, a function entry), GRM3 (`PushSt0`, cold), BT1 (the relocated `ret 14h` epilogue, full: memcpy), AG1-AG3 and PE0 (whole functions answered by a light callback, `Rs7Mid::Ret`; PE0 per note) | GSA1, GSC1, GIA1, MD1, GRM3, BT1, AG1-AG3, PE0 | mid-hook (`features/strings/hooks_mid.cpp`, `hooks_wrap.cpp`, `features/nslot/hooks_wrap.cpp`, `hooks_eval.cpp`, `features/pitch/hooks_mid.cpp`) |
| asm reduction part 2 (stubs frozen at `72da588`, `tools/tests/fixtures/midhook_72da588.json`, `tools/tests/test_midhook_asm_sites2.py`, frozen stubs and stock bytes): the per-frame tailed expected pitch of a tracked note (PC1, PC4: light call sites, Before; `Rs7Pitch_Tailed` reads the registry of tailed allocations instead of `_msize`), the standard-zero expected pitch (PUBZ0, PUBZ4-6 call sites, PUBZ1-3 jmp sites whose replay is the relocated `jmp`; one light callback for the old `Rs7_Publish_ZeroRaw`) and the PUBAN copy's call of the expected pitch over the tail (a light call entry built at copy time, `Rs7_Mid_PubTailPitch`, for the old `Rs7_Pitch_Raw8`) | PC1, PC4, PUBZ0-PUBZ6 | mid-hook (`features/pitch/hooks_calls.cpp`, `features/publish/hooks_pitch.cpp`) |
| bend direction guards (light mode); gem position log, chord frame marks (full mode, their helpers use SSE) | BD1-BD4; D5, B1 | mid-hook (refactor step 7, stubs frozen at `0cf5874`, `tools/tests/fixtures/midhook_0cf5874.json`) |
| string 6 in the SNG section mask (song load); native pitch tails at matcher creation, detector reset, matcher tuning | N12, N13; PM0, PR0, PM1 | mid-hook, full mode (refactor step 7 batch 2, stubs frozen at `24bfba8`, `tools/tests/fixtures/midhook_24bfba8.json`) |
| tuner camera reference string, slide end cell stride (light) | TF1, V2 | mid-hook (refactor step 7 batch 3, `features/tuner/hooks_camera.cpp`, `features/fretgrid/hooks_slide.cpp`; stubs frozen at `b8c0ed7`, `tools/tests/fixtures/midhook_b8c0ed7.json`) |
| N-slot template / ChordNotes redirects (one callback, a per-site register table); eval arrays, tails, mask test, child slots; technique-check latches (light; TP11, DS1 on the loader thread and PL1 with its FP pluck helper: full) | TP1-TP8, TP11, TP12, CP1, CP2, DS1; LU3, WC2, CS1, CC3, CH3, PL1; UP1-UP3, PD2-PD5 | mid-hook (batch 3, `features/nslot/hooks_records.cpp`, `hooks_eval.cpp`, `hooks_latches.cpp`; `tools/tests/test_midhook_nslot.py`) |
| N-slot sites that call game code from the live tracker state | RL1, RL2 | stub (reasons in the header of `features/nslot/hooks_stubs_tracker.cpp`) |
| calls written into the GuitarStrings copies, call-in entries: template record (TP2), glow ramp, headstock segment flag / colour, shape lookup (light); glow colour, attach point (full: float helpers); glow read (sse: the x87 result via `LoadSt0`, frozen at `06733ba`); the helper `Rs7_NsTplAt` went with TP2 | `Rs7_Call_TP2`, `_GlowSetup`, `_GlowColour`, `_HsLoaded`, `_HsSeg`, `_Attach`, `_Shape`, `_GlowRead` | call-in (`features/strings/hooks_callin.cpp`; helpers frozen at `304b9d3`, `tools/tests/fixtures/midhook_304b9d3.json`, `tools/tests/test_midhook_callin.py`) |
| calls written into the publication, pitch-reader and tuner copies, call-in entries: base pitch, wire count, reverse tail (light; `sub` flags via `Context::Compare32`); classification, chord tail, tuner edge, the session copies' stdcall steps (full: C helpers, per analysed note / frame / key press) | `Rs7_Call_PubBaseG`, `_PubBaseB`, `_PubClassify`, `_PubWireCount`, `_PubChordTail`, `_PubEdge`, `Rs7_Publish_ReverseTail`, `Rs7Tu8_Advance`, `Rs7Tu8_First` | call-in (`features/publish/hooks_callin.cpp`, `features/tuner/hooks_callin.cpp`; same fixture, `tools/tests/test_midhook_callin2.py`) |
| song destructor (call site, Before placement) | SF1 | mid-hook, full mode (`features/nslot/hooks_free.cpp`; stub frozen at `44135a2`, `tools/tests/fixtures/midhook_44135a2.json`) |
| jmp_atomic8 sites (one locked qword write over `mov [eax+1356h],r16; ret`) | S3, PT1 | mid-hook, full mode (`features/pitch/hooks_mid.cpp`; stubs frozen at `318618f`, `tools/tests/fixtures/midhook_318618f.json`) |
| string-6 lane position of spawned note effects (x87 result via `Context::PushSt0`) | R3a-R3c, R4 | mid-hook, full mode (`features/string7/hooks_lanes.cpp`; same fixture) |
| per-frame driver, string-6 bends while held (full: a few hits per frame); string-6 releases (light, integer-only Rs7_S7BendObject); inverse pitch of string 6 (full, once per analysed mistake) | N5, N18, N22; N19, N20, N23; I1 | mid-hook (batch 2b, stubs frozen at `6d1a62c`, `tools/tests/fixtures/midhook_6d1a62c.json`) |
| per-frame, per-note, audio thread, copied-function entries, custom conventions | S1 (rel32 branch target, helper not light-safe), S2 (legacy fallback without PE0), NGP (stack probe) | stub (hot, x87 or custom ABI) |
| cold or diagnostic, same shape as converted ones, not converted in this pass | (AT, LT1, AL1 went in batch 5) | - |
| feature stubs of refactor step 7 batch 5: chord loops (light); next-note grid NG1 (light), NG9; FretGrid FGRB; chord diagrams DGB, DGM, DGS, TP13; tuner entries TU8H/C/A and the 21 TS8*, TU8INIT, TU8FREE, AT; publication entries PUBCL/TO/WU/NL/AN/AC, guards PUBGS/GE1/GE2, PUBGC, PUBGO, PUBEDGE (light) and PUBGB, PUBGA, PUBGF; PegSet PSU1, PSU2, PSM1, LT1, AL1 (continues in a C wrapper); virtual guitarist TICK, JUD1, JUD2, AUD1; GuitarStrings entries GSFR-GSHC, GRM1, GRM2 | CLBM, CLBA, CLGU, CLGE, CLVB; NG1, NG9; FGRB; DGB, DGM, DGS, TP13; TU8H, TU8C, TU8A, TS8*, TU8INIT, TU8FREE, AT; PUB*; PSU1, PSU2, PSM1, LT1, AL1; TICK, JUD1, JUD2, AUD1; GS* entries, GRM1, GRM2 | mid-hook (`features/*/hooks_cl.cpp`, `hooks_mid.cpp`, `hooks_entries.cpp`, `hooks_gridsites.cpp`, `hooks_build.cpp`; stubs frozen at `9c0e44f`, `tools/tests/fixtures/midhook_9c0e44f.json`, `tools/tests/test_midhook_features.py`) |
| the plain-PSARC unit's jmp_atomic8 sites (cold: per archive / SNG load), written by the unit with its own qword CAS next to the CDLC enabler; the unit builds their entries with `Rs7_PrepareMid` (the whole `expect` replayed) | PP_TOC, PP_SNGF | mid-hook, full mode (asm reduction part 3, `features/psarc/hooks_mid.cpp`; stubs frozen at `fdfddcf`, `tools/tests/fixtures/midhook_fdfddcf.json`, `tools/tests/test_midhook_asm_sites3.py`) |

## 16. Source layout

Decided 2026-10-17; the rules are in [AGENTS.md "Code structure"](../AGENTS.md#code-structure) and
`tools/check_layout.py` checks them.

**Why feature modules on a shared core, not pure vertical slices.** Most features patch different game code and can
live in their own folder with their sites, hooks, logic, settings and tests (a vertical slice). The seven-string
features are not independent, though: they share one model of the instrument (string count, tunings, the
7-string unit that fails as a whole, section 3). That model is a layer of its own (`src/model/`), next to the
platform (`src/platform/`: patching, resolve, mid-hook, protection, log, config), so no feature duplicates it and
no feature reaches into another.

```
src/
  platform/           no game knowledge
  model/              the string-count model, no game addresses
  features/<name>/    <name>.h (public), logic (<name>.cpp, <part>.cpp), hooks*.cpp, registration.cpp, settings.cpp
  d3d/  overlay/      device hooks, F9 overlay
  startup.cpp         wires platform, model and features together: Rs7_Startup is a list of phases (config, wait
                      for the game, isolated features, resolve, bind, 7-string unit checks, patch rounds, report,
                      device worker); feature steps are the features' own Rs7<X>_Start / _Configure / _Prepare
  startup_sites.cpp   the main batch: site writers, isolated patch rounds, deferred FretGrid sites, per-site log
```

Inside a feature the logic is plain C++ over values (unit-tested natively), the hooks are thin glue between the
game's registers or memory and that logic. The site list stays one file (`sites.json`, `tools/gen_sites.py`); each
feature's hook rows are its own table (`registration.cpp`), and `src/registry.cpp` lists the tables once.

**Migration (done 2026-10-18).** The old layout (`src/core/`, `src/hooks/`) is gone; every file is in one of the
layers above. Where things ended up:

- `platform/`: patch, pattern, resolve, protect, sites, midhook (+ `midhook_wrap`, the MASM thunks), fncopy, log,
  retry, safecopy, config (ini helpers), settings (the store, no keys), uilayout, freeze, dxt, spinprobe, hooktable
  (`Rs7Hook` / `Rs7HookTable` and the lookup), isolate (the patch rounds; groups are plain integers, `Rs7Group`).
  No shared stub layer: the few remaining naked stubs declare themselves ("Remaining stubs", section 15).
- `model/`: eight, tuning, stringcolour, extblock (RS7X), status (overlay status board), groups (`Rs7Feature`,
  `Rs7_Group`, the 7-string unit).
- `features/`: allocstats, chords, clite, colours, diag, diagrams, fretgrid, frets, headstock, nextgrid, nslot, pitch,
  psarc, publish, scene, songend, string7, strings, testdrive, tuner, vguit, window - each with its hook table(s),
  settings table and tests. Every hook row lives in its feature; a group is its table's group (data references are
  Core), so `Rs7_FeatureOf` has no id or flag rules.
- App layer: `startup.cpp` / `startup_sites.cpp`, `registry.cpp` (table list, `Rs7_FindHook`, `kRs7Groups`),
  `hook_rules.cpp` (`Rs7_DisableFeature` cascade as a table, `Rs7_ShouldPatch`), `settings_tables.cpp`,
  `settings_removed.cpp`, `dinput_wrap`, `d3d/`, `overlay/`. The plain-PSARC unit gets the lookup and the groups from
  startup (`Rs7PlainHost`), so no feature includes the app layer.

Every move was a layout change with unchanged machine code (`scripts\compare_builds.ps1 -NoOpt`). The per-step history
is in the git log (pull requests #40 to #80).

**Moving or adding a feature** (`src/features/psarc/` is the template):

| File | Contents |
|---|---|
| `<name>.h` | The public header, the only one other code includes: the startup entry, what the overlay or log reads, `extern const Rs7HookTable kRs7<Name>Hooks;` (forward-declared `struct Rs7HookTable;`). Below it the logic's declarations, for the feature's tests. |
| `<name>.cpp`, `<part>.cpp` | Logic: values in, values out. No Win32, no game addresses, no registers. |
| `hooks.cpp`, `hooks_<part>.cpp` | Glue: the stubs / mid-hook callbacks and the code that resolves and writes the sites. Reads its own ini keys. |
| `settings.cpp` | The feature's settings table: its `Rs7SetDef` rows (section, key, type, when, default, range, overlay label, help, the live `apply` and optionally `startup`) as `const Rs7SetTable kRs7<Name>Settings = {kDefs, count, Read};`, and its typed startup read (`Rs7<Name>_ReadIni`, a struct with the values and their validity for the log, kept for `Rs7<Name>_Config()`). Where it has overlay controls, its overlay items (`const Rs7UiTable kRs7<Name>Ui`, "Overlay tabs" below). |
| `registration.cpp` | The feature's `Rs7Hook` entries in an anonymous-namespace array, exported as `const Rs7HookTable kRs7<Name>Hooks = {Rs7Feature::<Group>, kHooks, count};`. |
| `tests/<name>/` | Native tests (`TEST(...)`, the test project compiles `tests/**`). |

1. Move the files with `git mv` (history follows) and keep function names, so `scripts\compare_builds.ps1 -NoOpt` matches
   them; `-Alias` only for code that leaves an anonymous namespace for a named one.
2. Put the feature's rows into `registration.cpp` and add its table to `kTables` in `src/registry.cpp` (one line).
   `Rs7_FindHook` searches every table; `Rs7_FeatureOf` returns the table's group. Write conditions are in
   `Rs7_ShouldPatch` (`src/hook_rules.cpp`).
3. Startup calls the feature through `<name>.h` only. Reading the feature's ini keys moves into the feature
   (`Rs7Plain_StartFromIni`); its keys go into its settings table (`settings.cpp`, below).
4. Update the paths in `tools/` (generators, `tools/tests/`), `tools/layout_baseline.json` (a moved entry keeps its
   size under the new path), AGENTS.md and this document.
5. Gates: `scripts\format.ps1`, `build.ps1` (includes `check_layout`), pytest, `sitecheck`, `check_exports`, and
   `scripts\compare_builds.ps1 -Old origin/main -New HEAD -NoOpt` with every difference listed in the pull request.

**Settings.** `platform/settings` is the settings store the overlay and the pipe commands edit; it knows no key. Every
feature owns an `Rs7SetTable` (above); `src/settings_tables.cpp` lists every table once (`kRs7SettingTables`, like
`kTables` for the hooks). Decided 2026-10-18: one table per feature and the list as the only central order. The list
order is the order of the keys (`Rs7Set_Def`, `Rs7Set_Save`) and of the startup reads; the release template
(`scripts\package.ps1 -TemplateOnly`, and with it `docs/release/SETTINGS.md`) orders the ini sections itself and keeps
the list order inside a section, so the list is ordered to keep each section's key order of 1.0 (the comment in
`settings_tables.cpp` says how). Per-key order numbers would be a second order to keep in step; a central list of keys
would be the old catalogue again. `Rs7Set_ReadIni` runs every table's `read` (startup, and again in
`Rs7Set_InitFromIni`, as before); a row's `startup` hands the feature's own validated value to the store (string 6's
colourblind colour follows string 6's colour, the atlas row, the fret half length, the autotune clamp). The keys removed in
1.0 are in `src/settings_removed.cpp` (a table without rows whose read logs each one present), the overlay's own in
`overlay/overlay_settings.cpp`. Keys read only by their feature at startup (`[chords] frame_axis`,
`[test] vguitar*`, ...) have no row; `scripts/package.ps1` lists them for the template.

**Overlay tabs.** Each feature also owns its part of the F9 overlay, as data: an `Rs7UiTable` (`platform/uilayout.h`)
next to its settings table in `settings.cpp`, exported as `kRs7<Name>Ui`. Its items say which of its settings show, in
which tab, in which order, under which section titles, and its live rows (a `Row` or an `OnOff` with a small reader
that formats the feature's counters). `src/settings_tables.cpp` lists every table once (`kRs7UiTables`); a tab shows
its items in list order. `overlay/ui.cpp` keeps the generic part: the tab bar, a setting's control from its
`Rs7SetDef` (type, label, help, badges), the status board (build, sites, song, overlay) with the features' `Status`
items under "features", and the log. Decided 2026-10-18: data instead of per-feature ImGui functions, so features do
not include ImGui and the layout has native tests (`tests/test_uilayout.cpp` pins every tab's items). A setting without
an item (`[window] console_quickedit_off`, ...) has no control, as before. The item's tab is also the player/advanced
split: `Rs7UiTab::Player` items (`Rs7UiPlayer(id, label, hint)`, a plain label and a one-line hint the test checks for
key names and developer terms) form the default view; everything in the real tabs is under the "Advanced" tab.
Decided 2026-10-19: the split is data in the feature's table, not a list in `ui.cpp`. Decided 2026-10-19 (after the
first player view scrolled too much): each `Rs7UiPlayerSection(title, tabLabel)` in the Player items is one tab of the
overlay's single tab bar (`Rs7Ui_PlayerTabs`), Advanced is the last tab, and a hint is a `(?)` tooltip.

Both projects compile `src/features/**` into one object folder per source folder (`ObjectFileName`), because every
feature has a `hooks.cpp` and a `registration.cpp`.

**Inline assembly.** A patch site needs a few instructions that save the game's registers and enter C++; MSVC
x86 C++ cannot express that, so that code exists, but it should exist once (the mid-hook thunks, now MASM in
`src/platform/midhook_thunks.asm`, section 15) or come from a generator, not be written per site. Plan, in this order:

1. Relocator: the operand-size prefix and `inc` / `dec r8` forms (about 30 sites rejected today, section 15).
2. Call-kind sites: relocate a `call` by pushing the original return address and jumping to the target.
3. Wrappers: C++ detours through typed function pointers.
4. Done: the light mid-hook mode (section 15, about 25 ns instead of 150 ns per hit, no floating-point save, all
   GPRs kept so the `Context` stays identical). Measure one hot site in game before converting hot sites.
5. Done: `stub_macros.h` is gone; what remains is listed in the maintainer's handover notes (custom calling conventions,
   game calls with register arguments).

Every conversion keeps its emulator comparison against the stock bytes.
