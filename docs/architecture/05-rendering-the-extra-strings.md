# 5. Rendering the extra strings

Part of the [rs7string architecture](../ARCHITECTURE.md).

The seventh string is represented in the native grid and string layouts. Rendering combines that
geometry with colour tables, note ramps and a locally built headstock.

| Part | Responsibility |
|---|---|
| FretGrid | Gives the extra lane a real cell for gems and note tails. |
| Colour manager and ramps | Supply the string colour to the game and keep RSMods compatibility. |
| String and fret geometry | Draw the extra string and extend fret bars across the instrument. |
| Headstock | Builds and selects the matching model from the player's installation. |

The sections below give the addresses, test evidence and open hypotheses for each part.

## Lane and gems

String 6 is a real FretGrid cell; its lane lies above the low E lane
(`z = 2*z(string 0) - z(string 1)` before the native grid). Gem colour (`0x7A8590`), NoteFx
sparks (`0x40CAD5`), hit/miss text (`0x7F2FDD`) and the Riff Repeater marker (`0x79BBFD`) read
string 6's colour from the native eight-column colour manager (CM* sites). Note tails (sustain ribbons, slides, vibrato, bends) follow the 7th lane
because the native grid gives string 6 a real cell; BD1-BD4 give string-6 bends string 0's direction.

## RSMods colour compatibility

CONFIRMED by source and native regression tests: RSMods
`GetStringColor` reads `*0x135F58C`, player mode at `+0x348`, then uses mode stride `0xA8`
and six-column state offsets starting at `+0x350`. Widening those tables in place redirects its
custom-colour, Extended Range and rainbow writes to other states/modes. The manager keeps
the stock RGB-pointer/UV ABI at `+0x350` / `+0x890` and appends eight-column RGB-pointer/UV tables
at `+0xDD0` / `+0x14D0` (allocation and sized free `0x1BD0`). CMINIT/CMBUILD and palette publication
mirror the first six pointer entries of every state and mode into the stock tables. Both views
alias the same source RGB storage, so RSMods writes immediately reach the corresponding game
colour, including colourblind mode, named peg colours and fret-number states. The extra strings
remain owned by rs7string. Game readers/writers use the appended tables. In-game confirmation
with the reported custom/rainbow settings and 3+3 headstock is still required.

### Selected peg refresh

CONFIRMED by game code and native tests: `0x59BD70` / `0x59BF80`
copy a named colour into each PegSet slot at `+0x10`; palette edits do not invalidate it.
RSMods deliberately delays its tuner palette application. N5 refreshes these copies on the
game thread for both six- and seven-string guitars, using the last PSN1 session's player index.
Names 0/1 retain reset/success states 6/5; string names use state 4. The seventh peg recalculates
its brightness from the current stock state-4/state-1 ratio. Taken flags, named indices and animation scalars at `+0x20/+0x24` stay intact.
`0x543150` also writes this copied RGB times `ClipFade*2` into `StringColor` before the
`0x543910` glow blur, so stale RGB affects both the peg and its coloured bloom. The black quads
at `0x544610` are a separate path. **HYPOTHESIS:** the RGB/intensity mismatch explains the
reported difference in glow width; verify the appearance in the same tuner stage in game.

The pass doubles that RGB, so `NamedColour` caps the seventh peg's brightest channel at 0.5 (the
peak of the stock named colours logged in game): with RSMods' 00C68E and a state-4/state-1 ratio near 1
the doubled colour exceeded 1 in two channels and the glow turned cyan (HYPOTHESIS: clamped by an 8-bit
target; the ×2 is CONFIRMED in `0x543150`, the target format is not).

### Seventh-string default

CONFIRMED by RSMods source and native tests: at startup,
inspect the game directory's XInput proxy as resource data, identify its RSMods version resource, and read
the adjacent `RSMods.ini`. This works before RSMods loads and avoids the same-named system XInput DLL.
With `ExtendedRange=on`, use `string0_CB` for custom-colour mode 2, or RSMods' default ER low B
`00C68E` for modes 0/1. Apply it to both profile colour modes. Missing/invalid configuration falls
back to rs7string's own colours. `[color] rsmods_extended_range=0` explicitly selects those saved
colours; toggling the preference never overwrites them. RSMods configuration changes need a restart.

### Pure red glow

CONFIRMED by RSMods source `CollectColors.cpp` and emulator tests; not yet seen in game. RSMods'
custom string colours write each string's Glow every tick as HSL(hue - 4, 1, 0.5) of
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

## Tuner menu entry

CONFIRMED by live log and generated-copy emulator: TS8M2E at `0x5B0135`
normalizes a menu start (submode 0/string 0) to `order[0]`. It must update both EDI and the string
argument at `[ebp+0xC]`: entering from old mode 5 reloads that argument at `0x5B020E` and selects
again at `0x5B0213`. Updating EDI alone selects low B and then E while the stored current string
still says low B. The generated port preserves the normalized argument for that second call;
fallback submode 1 and explicit reverse-pass starts retain their requested string.

## Note colour

The game colours a note by sampling a 256x128 ramp texture (16 rows, one per string and
colour mode) at a V coordinate. Step 1 writes the exact `[color]` RGB for sparks and text. Step 2
(always on since 1.0): a worker walks the engine's texture list under the game's lock (list head
`0x1360B00`, lock `0x1383D74`, `next` at `+0x34`, renderer data `+0x2C`, the `IDirect3DTexture9*` at
renderer data `+0x64`), collects `note_gradient_map.dds` / `gradient_map_additive.dds`, patches slots
64/65 (`GetTexture` / `SetTexture`) of the game device's vtable about 15 s after `d3d9.dll` appears,
and on the first bind copies each ramp into a same-size MANAGED texture where only the lines at
V6 = 57/128 (normal) and 121/128 (colourblind) are repainted. A self-check (CPU plus read-back)
must pass before string 6 switches to V6; anything unexpected keeps V6 at 0 so string 6 shows the
stock grey preview row and strings 0-5 stay stock. d3d9.dll gives each device its own vtable copy and
rewrites it at times (e.g. at song load); the worker re-checks every second. Limits: with 3 or more
mip levels the colour is pure only on mips 0-1.

### RSMods note ramps

CONFIRMED by RSMods source `D3DHooks.cpp` / `Settings.cpp` and the in-game log: with custom string
colours or Extended Range, RSMods' draw hooks bind their own generated ramp
(256x128 A8R8G8B8 MANAGED, rows 6/7 and 14/15 fixed greys) on stage 1 after the game bound the stock
one, so string 6 at V6 sampled RSMods' grey row 7 / 15. When the installed `xinput1_3.dll` identifies as
RSMods (`rs7rsmods::Present`) and V6 is on, the SetTexture hook gives each such texture (stage 1,
256x128 A8R8G8B8 MANAGED, rows 7 and 15 grey) its own copy with the V6 lines repainted, with the same
build and self-check (any failure: RSMods' texture stays bound), hides it from `GetTexture` and holds a
reference on the source (a rebuilt RSMods texture is a new address; at most 8 cached). A colour change
rebuilds them at the next bind. Limit: more than 32 distinct RSMods ramps within ten seconds (e.g. rainbow notes) turn this
path off for the session; string 6 then shows RSMods' row again.

### Source files

The ramp swap code lives in `src/d3d/` (public interface `swap.h`): `swap_hook.cpp` (vtable slots 64/65, thunks,
re-check, GetTexture hiding; `HookState`), `swap_ramp.cpp` (candidate processing, recolour, retire, overlay
status; `RampState`), `swap_foreign.cpp` (the RSMods ramps above), `swap_texio.cpp` (level reads, copy build
with self-check, write with read-back), `hookutil.cpp` (log and module-name helpers shared with `present.cpp`).
Tests: `tests/test_d3d_hook.cpp`, `test_d3d_ramp.cpp`, `test_d3d_foreign.cpp` on `tests/swap_fixture.h`.

The colour code lives in `src/features/colours/` (public interface `colours.h`; the D3D side above includes only
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

## Seventh-string line

With `string7_line`, code in `src/features/strings/` and per-guitar state in `src/features/string7/`, string 6
is a stock 0x110 string object of the native GuitarStrings (slot 6 of its capacity-8 arrays, ghost included),
driven by the stock string loops; atlas row V = 0.4, string 0's row (`rs7s7::kDefaultAtlasRow`; 0.0-0.3 are
the bass rows). The game's own section dimming lights it (SNG section string mask bit 6: N12 `0x55CC71` / N13
`0x55CE3A`); bends N18-N20 (detected bend `bendup_NN` through `Rnd_GuitarString_SetBend` `0x598910`; the chart bend on
the ghost string, `ghost_up_NN`, is CLGU/CLGE) and the chord pluck PL1 reach the native string 6. Limit: after `Rnd_GuitarStrings_ResetState` `0x59AC20`
(restart/seek) a running ghost bend is only released by the next string-6 note end.

## Fret bars

In `src/features/frets/` (F1 `0x58FB20`), on a guitar built with 7/8 strings every bar (and
the nut) covers the extra lanes: one for seven strings, two for eight (`rs7frets::Compute` takes the shift and
scale from the real lane positions). For seven strings it moves half a string spacing towards string 6 and is
scaled uniformly by `k = (L + d/2)/L` with L = 4.61 (`rs7frets::kDefaultHalfLen`, the mesh's half length); measured d 1.6079,
k 1.1744. Bars are about 17 % thicker. Locators and span data are never written. Chord frames
(B1 `0x7A2BCA`, B2 `0x7A91E4`) get a string-axis scale and shift from the guitar's lane positions; on 7/8-string
guitars they always reach the last string, like the extended bass's.

The fret glow at a hand position (`Rnd_FretRangeFx_Init` `0x40DA40`) lights the wires at both ends of the range with
four fixed-size bar effects (+70h..+7Ch) and draws the cyan box between them (+6Ch, effect `FA400094`, a skinned
mesh whose `bone_root` / `bone_tail` sit on the two fret centres, so its width follows the frets and its height is
the stock model's). The game only centres these pieces between the wire ends; the effect's own scale field (+14h,
sent at creation) does not change their size (CONFIRMED in game: doubling it changed nothing). FRG1 (`0x40DCCD`)
moves the wire ends beyond string 0 out to the floor, so the centre follows all strings, and records the five pieces
with the uniform entity scale `rs7frets::GlowStretch` = (all strings' width + 2 margins) / (the stock 8.04 + 2
margins), from the real string spacing and the stock margin beyond string 0 (5 x 1.608 guitar, 3 x 2.68 bass), so
`[neck] spacing_*` is followed. The bars get it in the effect update `0x40B8D0` after a bar is placed: FRG2
(`0x40BAE3`, a bar being shown) and FRG2B (`0x40BC60`, a bar being moved, the path the fret glow takes). The box gets
it at FRG3 (`0x40C5E7`, the box update `0x40C590` right after its entity is first placed). CONFIRMED in game
(2026-11-13, #283): before FRG2B / FRG3 neither the bars nor the box were ever scaled (no FRG2 call in a whole song)
and the 8-string box ended between the 7th and 8th string; now it spans 8.3 lanes (x1.268) and ends just below the
8th string, with the same margins as the stock 6-string glow (a little more above the top string than below the
lowest).

## Headstock model

In `src/features/headstock/assetload*` and `hsbuild.*` (`[assets] headstock7`), the guitar and its
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
  template occurs in the literals. A build takes about 0.2 s inside the loader thread. The build runs
  when a model file is missing or when the package next to the DLL changed since the last build (an update;
  each build writes `<package>.stamp` with the package's size and write time into the model folder). When it
  runs, existing files are compared against the manifest hashes and any that do not match are rebuilt. A
  `headstock7_file` the player set is never replaced. Failure keeps the stock headstock. The left-handed frame
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
  `0x543150` tints its knob like the others. The pass draws the mesh list `Rnd_StringRenderer_Setup` `0x541880` collected per
  peg clone with NiNode `0x9E1360`, which ignores the cull flag, and never checks the flag itself
  (CONFIRMED by disassembly): every 3+3 clone carries both the bakelite knob and `rs7_alt`, so the
  hidden knob got the glow too (the wider bakelite shape). PGH1 `0x5431DC` skips a mesh whose
  culled ancestor is `rs7_alt` or one of its siblings (`Rs7Asset_PegMeshHidden`).
- Limits: the stock headstock shows for about 0.1 s when the pre-song tuner hands over to the song;
  with the reversed string order the headstock strings 1-6 fade out before the posts do.
- The guitar frame carries the 8-peg model as `rs7_alt` and the 7-peg one as `rs7_alt5`
  (`g_file7s`, like the 5-string bass); the head shows the one the guitar is built with. The stock binder seats
  peg slot 6 on the frame's first `Locator_Peg_7`, possibly the hidden model's and possibly after our locator
  swap (CONFIRMED in game 2026-10-29: the B peg sat at the 8-peg model's post, next to E), so `KeepExtraLocators`
  re-points the seated extra slots to the shown model every shown frame.
- Tuner camera (TF2 `0x5B297F`): the stock overview framing fits six pegs; a right-handed guitar built with 7/8
  strings, or a 5/6-string bass, pans 6 units along the neck (world y, `[EBP-24h]`) so the outer pegs and letters
  stay on screen (CONFIRMED in game: the high e, and the six-string bass's high C, fell off the left edge).
  Left-handed views have the room. TF1 picks the reference string at the edge opposite string 0: a five-string
  bass's string 4 is the low B next to string 0, so it uses string 3 (CONFIRMED in game 2026-10-31: the view sat
  low and co-op cut off the bass's D and G pegs).
- Song camera (SC1 `0x593A1A`, full mid-hook in `src/features/frets/hooks_camera.cpp`): all four outgoing
  camera markers follow the highway floor's z shift. CONFIRMED by the send path `0x593980`: three z values
  are at `[ESP+18h/24h/30h]`; EDX holds the fourth z as float bits from guitar `+34h`, stored to
  `[ESP+3Ch]` after the hook. The computed pair at `0x5938A0` alone does not cover every marker.
  `rs7fretglue::FloorOffset` is shared with the floor and calls `FloorMove` using the actual string-0/1
  attach points and the stock local bar-B locator. It moves by `nearLanes * (a0-a1)` beyond string 0, or
  `-farLanes * (a0-a1)` beyond the stock far string: 1/2 near lanes for 7/8-string guitar, one for 5/6-string
  bass, and one far lane for six-string bass's high C. Handedness (`InstrumentInfo +9`) and inverted string
  order (`+8`) are independent; the attach geometry determines the side and sign. Stock six-string guitar,
  four-string bass and `g_rs7_frets_on=0` leave the markers unchanged. Missing attach points or an unready
  bar leave this send unchanged and are retried at the next send; no guessed spacing is used. Only outgoing
  values change, so repeated sends do not accumulate a shift. HYPOTHESIS pending in-game checks: following
  the floor restores the stock screen position of the fret numbers in every song/co-op camera pose.
- Unverified input: an arbitrary custom NIF configured through `headstock7_file` is passed to the
  game's NIF handler after a size check only (HYPOTHESIS: a malformed file can still damage the stock
  parser).

<details markdown="1">
<summary><b>Reference counts</b></summary>

Edited-model holders own up to 117 references (`altHold[6][9]`, `params7[9]`,
`stockHold[6][9]`). Guitar release destroys the headstock BEFORE `Rnd_GuitarStrings_Free` `0x599D70`
(CONFIRMED `0x58D9E0`, `0x592770`). NIF/DDS handler wrappers start with a zero count, vtable `+4` is
AddRef (`0x88A200`) and `+8` Release (`0x8A53D0`); calling NiObject DeleteThis on them calls AddRef.
`Rnd_SetStringShaderParams` `0x5998F0` owns nine output slots (assignment increments the LONG at `+4`,
zero calls vtable `+4`); the stock holder destructor `0x58DAB0` releases them last slot first.

</details>
