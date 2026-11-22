# 5. Rendering the extra strings

Part of the [RS Extended Range architecture](../ARCHITECTURE.md).

The native grid and string layouts represent the seventh string. Rendering combines that
geometry with color tables, note ramps and a locally built headstock.

| Part | Responsibility |
|---|---|
| FretGrid | Gives the extra lane a real cell for gems and note tails. |
| Color manager and ramps | Supply the string color to the game and keep RSMods compatibility. |
| String and fret geometry | Draw the extra string and extend fret bars across the instrument. |
| Headstock | Builds and selects the matching model from the player's installation. |

The sections below give the addresses, test evidence and open hypotheses for each part.

## Lane and gems

String 6 is a real FretGrid cell. Its lane lies above the low E lane
(`z = 2*z(string 0) - z(string 1)` before the native grid). Gem color (`0x7A8590`), NoteFx
sparks (`0x40CAD5`), hit/miss text (`0x7F2FDD`) and the Riff Repeater marker (`0x79BBFD`) read
string 6's color from the native eight-column color manager (CM* sites). Note tails (sustain ribbons,
slides, vibrato, bends) follow the 7th lane, because the native grid gives string 6 a real cell. BD1-BD4
give string-6 bends the direction of string 0.

## RSMods color compatibility

CONFIRMED by source and native regression tests: RSMods
`GetStringColor` reads `*0x135F58C` and the player mode at `+0x348`. It then uses mode stride `0xA8`
and six-column state offsets starting at `+0x350`. Widening those tables in place would send RSMods'
custom-color, Extended Range and rainbow writes to other states/modes. So the manager keeps
the stock RGB-pointer/UV ABI at `+0x350` / `+0x890`. It appends eight-column RGB-pointer/UV tables
at `+0xDD0` / `+0x14D0` (allocation and sized free `0x1BD0`). CMINIT/CMBUILD and palette publication
mirror the first six pointer entries of every state and mode into the stock tables. Both views
alias the same source RGB storage. RSMods writes therefore reach the matching game color at once,
including colorblind mode, named peg colors and fret-number states. The extra strings
remain owned by RS Extended Range. Game readers/writers use the appended tables. In-game confirmation
with the reported custom/rainbow settings and 3+3 headstock is still required.

### Selected peg refresh

CONFIRMED by game code and native tests: `0x59BD70` / `0x59BF80`
copy a named color into each PegSet slot at `+0x10`. Palette edits do not invalidate this copy.
RSMods deliberately delays applying its tuner palette. N5 refreshes these copies on the
game thread for both six- and seven-string guitars. It uses the player index of the last PSN1 session.
Names 0/1 keep reset/success states 6/5. String names use state 4. The seventh peg recalculates
its brightness from the current ratio of stock state 4 to state 1. Taken flags, named indices and
animation scalars at `+0x20/+0x24` stay intact.

`0x543150` also writes this copied RGB times `ClipFade*2` into `StringColor` before the
`0x543910` glow blur. Stale RGB therefore affects both the peg and its colored bloom. The black quads
at `0x544610` are a separate path. **HYPOTHESIS:** the RGB/intensity mismatch explains the
reported difference in glow width. Verify the appearance in the same tuner stage in game.

The pass doubles that RGB, so `NamedColour` caps the brightest channel of the seventh peg at 0.5.
That is the peak of the stock named colors logged in game. With RSMods' 00C68E and a state-4/state-1
ratio near 1, the doubled color exceeded 1 in two channels, and the glow turned cyan. HYPOTHESIS: an
8-bit target clamps the value. The ×2 is CONFIRMED in `0x543150`, but the target format is not.

### Seventh-string default

CONFIRMED by RSMods source and native tests: at startup, the mod inspects the XInput proxy in the game
directory as resource data. It identifies the proxy's RSMods version resource and reads the adjacent
`RSMods.ini`. This works before RSMods loads, and it avoids the system XInput DLL of the same name.
With `ExtendedRange=on`, the mod uses `string0_CB` for custom-color mode 2, or RSMods' default ER low B
`00C68E` for modes 0/1. It applies this color to both profile color modes. If the configuration is
missing or invalid, the mod falls back to RS Extended Range's own colors. `[color] rsmods_extended_range=0`
explicitly selects those saved colors. Toggling the preference never overwrites them. RSMods
configuration changes need a restart.

### Pure red glow

CONFIRMED by RSMods source `CollectColors.cpp` and emulator tests, but not yet seen in game. With custom
string colors, RSMods writes each string's Glow every tick as HSL(hue - 4, 1, 0.5) of
its Enabled color (int hue from its `RGB2HSL`). For a hue of 0..3 (e.g. `800000`), the result is
negative. RSMods' `HSL2RGB` then matches no sextant and keeps a stale near-black color, so the string's
glow outline is invisible.

RS Extended Range corrects the read side, not RSMods' table, because RSMods rewrites the table every tick.
`Rnd_StringRenderer_UpdateGlow` `0x5441A0` loads the glow RGB pointer at `0x544384` and copies it at
`0x54438B`. Two hooks replace that pointer with the answer of `Rsxr_GlowFixPtr`: GLW (mid-hook at
`0x54438B`, stock function) and `Rsxr_CallIn_GlowColour` (the GSGL copy's call-in). `Rsxr_GlowFixPtr`
answers with a buffer holding HSL((hue - 4 + 360) % 360, 1, 0.5) for strings 0-5 whose Enabled color
(state 1, same mode, stock or native layout) has an RSMods hue of 0..3. It uses RSMods' own conversions,
float for float (`src/features/colours/glow.cpp`). Any other color keeps the pointer.

The correction runs only with `g_rsxr_glow_fix`, which requires that the XInput proxy is RSMods and that
its `RSMods.ini` has `CustomStringColors=2` without `UseExactColors=on` (read once at startup). Stock and
Zag colors are therefore never touched. GLW is its own failure group (Glow), so a failed site turns off
only the correction.

## Tuner menu entry

CONFIRMED by live log and generated-copy emulator: TS8M2E at `0x5B0135`
normalizes a menu start (submode 0/string 0) to `order[0]`. It must update both EDI and the string
argument at `[ebp+0xC]`, because entering from old mode 5 reloads that argument at `0x5B020E` and selects
again at `0x5B0213`. If only EDI is updated, the tuner selects low B and then E, while the stored current
string still says low B. The generated port keeps the normalized argument for that second call.
Fallback submode 1 and explicit reverse-pass starts keep their requested string.

## Note color

The game colors a note by sampling a 256x128 ramp texture at a V coordinate. The texture has 16
rows, one per string and color mode. The mod sets the note color of string 6 in two steps.

Step 1 writes the exact `[color]` RGB for sparks and text.

Step 2 (always on since 1.0) recolors the ramp textures:

1. A worker walks the engine's texture list under the game's lock (list head `0x1360B00`, lock
   `0x1383D74`, `next` at `+0x34`, renderer data `+0x2C`, the `IDirect3DTexture9*` at renderer data
   `+0x64`).
2. It collects `note_gradient_map.dds` / `gradient_map_additive.dds`.
3. About 15 s after `d3d9.dll` appears, it patches slots 64/65 (`GetTexture` / `SetTexture`) of the
   game device's vtable.
4. On the first bind, it copies each ramp into a same-size MANAGED texture. In the copy, only the lines
   at V6 = 57/128 (normal) and 121/128 (colorblind) are repainted.

A self-check (CPU plus read-back) must pass before string 6 switches to V6. If anything is unexpected,
V6 stays at 0. String 6 then shows the stock gray preview row, and strings 0-5 stay stock. d3d9.dll gives
each device its own vtable copy and rewrites it at times (e.g. at song load), so the worker checks it
again every second. Limits: with 3 or more mip levels, the color is pure only on mips 0-1.

### RSMods note ramps

CONFIRMED by RSMods source `D3DHooks.cpp` / `Settings.cpp` and the in-game log: with custom string
colors or Extended Range, RSMods' draw hooks bind their own generated ramp on stage 1, after the game
bound the stock one. That ramp is 256x128 A8R8G8B8 MANAGED, with rows 6/7 and 14/15 fixed grays. String 6
at V6 therefore sampled RSMods' gray row 7 / 15.

The mod handles this when the installed `xinput1_3.dll` identifies as RSMods (`rsxrrsmods::Present`) and
V6 is on. The SetTexture hook then gives each such texture (stage 1, 256x128 A8R8G8B8 MANAGED, rows 7 and
15 gray) its own copy with the V6 lines repainted. It uses the same build and self-check. On any failure,
RSMods' texture stays bound. The hook hides the copy from `GetTexture` and holds a reference on the source.
A rebuilt RSMods texture is a new address, and at most 8 are cached. A color change rebuilds the copies
at the next bind. Limit: more than 32 distinct RSMods ramps within ten seconds (e.g. rainbow notes) turn
this path off for the session. String 6 then shows RSMods' row again.

### Source files

The ramp swap code lives in `src/d3d/` (public interface `swap.h`):

- `swap_hook.cpp`: vtable slots 64/65, thunks, re-check, GetTexture hiding; `HookState`.
- `swap_ramp.cpp`: candidate processing, recolor, retire, overlay status; `RampState`.
- `swap_foreign.cpp`: the RSMods ramps above.
- `swap_texio.cpp`: level reads, copy build with self-check, write with read-back.
- `hookutil.cpp`: log and module-name helpers shared with `present.cpp`.

Tests: `tests/test_d3d_hook.cpp`, `test_d3d_ramp.cpp`, `test_d3d_foreign.cpp` on `tests/swap_fixture.h`.

The color code lives in `src/features/colours/` (public interface `colours.h`). The D3D side above includes
only that header.

- Logic: `rgb.cpp` (color text, channels, ramp-line recolor), `native.cpp` (the eight-column layout and
  `Publish`), `ramp.cpp` (the ramp copy model), `rampstate.cpp` (candidate table, V6 rule), `rsmods.cpp`
  (RSMods proxy and `RSMods.ini`), `glow.cpp` (RSMods' glow conversions).
- Glue: `hooks.cpp` (CMUV / CMIX / CMFALL, GLW, `Rsxr_GlowFixPtr`, `RsxrColour_Start` /
  `RsxrColour_CheckManager`), `hooks_publish.cpp` (CMINIT / CMBUILD and which thread may write the manager).
- State: `state.h` (one `State`).
- Registration: `registration.cpp` (`kRsxrColourHooks` with the generated `colour_hooks.generated.h` from
  `tools/gen_colournative.py`, group Colours; `kRsxrGlowHooks`, group Glow).
- Settings: `settings.cpp` holds the `[color]` settings table and its startup read (`RsxrColour_ReadIni`).
  `LoadConfig` in startup still logs the values.
- Tests: `tests/colours/`.

`g_rsxr_v6`, `g_rsxr_fx_rgb` (`native.cpp`) and the data references X_CM, X_TCS, X_THEAD
(`kRsxrColourRefHooks`, group Core) also belong to the feature. The CM / GLW write conditions stay in
`Rsxr_ShouldPatch` (`src/hook_rules.cpp`) with the other write conditions.

## Seventh-string line

With `string7_line`, string 6 is a stock 0x110 string object of the native GuitarStrings. The code is in
`src/features/strings/`, and the per-guitar state is in `src/features/string7/`. String 6 uses slot 6 of the
capacity-8 arrays, ghost included, and the stock string loops drive it. Its atlas row is V = 0.4, which is
string 0's row (`rsxrs7::kDefaultAtlasRow`; 0.0-0.3 are the bass rows). The game's own section dimming
lights it (SNG section string mask bit 6: N12 `0x55CC71` / N13 `0x55CE3A`). These also reach the native
string 6:

- bends N18-N20 (detected bend `bendup_NN` through `Rnd_GuitarString_SetBend` `0x598910`)
- CLGU/CLGE (the chart bend on the ghost string, `ghost_up_NN`)
- the chord pluck PL1

Limit: after `Rnd_GuitarStrings_ResetState` `0x59AC20` (restart/seek), only the next string-6 note end
releases a running ghost bend.

## Fret bars

In `src/features/frets/` (F1 `0x58FB20`), every bar and the nut on a guitar built with 7/8 strings
cover the extra lanes, one for seven strings and two for eight. `rsxrfrets::Compute` takes the shift and
scale from the real lane positions. For seven strings, a bar moves half a string spacing towards string 6.
It is scaled uniformly by `k = (L + d/2)/L`, with L = 4.61 (`rsxrfrets::kDefaultHalfLen`, the mesh's half
length). Measured values: d 1.6079, k 1.1744. Bars are about 17 % thicker. Locators and span data are never
written. Chord frames (B1 `0x7A2BCA`, B2 `0x7A91E4`) get a string-axis scale and shift from the guitar's lane
positions. On 7/8-string guitars they always reach the last string, like the extended bass's.

The fret glow at a hand position (`Rnd_FretRangeFx_Init` `0x40DA40`) has five pieces. Four fixed-size bar
effects (+70h..+7Ch) light the wires at both ends of the range. Between them, it draws the cyan box (+6Ch,
effect `FA400094`). The box is a skinned mesh whose `bone_root` / `bone_tail` sit on the two fret centers, so
its width follows the frets and its height is the stock model's. The game only centers these pieces between
the wire ends. The effect's own scale field (+14h, sent at creation) does not change their size (CONFIRMED in
game: doubling it changed nothing).

FRG1 (`0x40DCCD`) moves the wire ends beyond string 0 out to the floor, so the center follows all strings. It
also records the five pieces with the uniform entity scale `rsxrfrets::GlowStretch` = (all strings' width + 2
margins) / (the stock 8.04 + 2 margins). The scale comes from the real string spacing and the stock margin
beyond string 0 (5 x 1.608 guitar, 3 x 2.68 bass), so `[neck] spacing_*` is followed. The bars get the scale
in the effect update `0x40B8D0` after a bar is placed: FRG2 (`0x40BAE3`, a bar being shown) and FRG2B
(`0x40BC60`, a bar being moved, the path the fret glow takes). The box gets it at FRG3 (`0x40C5E7`, the box
update `0x40C590` right after its entity is first placed).

CONFIRMED in game (2026-11-13, #283): before FRG2B / FRG3, neither the bars nor the box were ever scaled (no
FRG2 call in a whole song), and the 8-string box ended between the 7th and 8th string. Now it spans 8.3 lanes
(x1.268) and ends just below the 8th string. It has the same margins as the stock 6-string glow (a little more
above the top string than below the lowest).

## Headstock model

The code is in `src/features/headstock/assetload*` and `hsbuild.*` (`[assets] headstock7`). The guitar and
its frame entity load once at boot. So the 7-string model (brandless "Facet", authored for this project) is
merged next to the stock one. When the game's nif handler (AL1 `0x4A8560`) loads a guitar frame, the mod
loads the model with the same handler under the same resource name. It renames the model `rsxr_alt`, culls
it and attaches it as the last child of the stock top node. The stock part comes first, so name lookups
(`string_size_%d`, `Locator_Peg_%d`) still bind stock nodes.

While the guitar shows the 7th string:

- `rsxr_alt` is shown and the stock sibling is culled.
- The six segment param objects point at the model's `string_size_1..6`.
- `string_size_7` gets its own params and the 7th string's color every frame (as `0x58E4F0` does for the
  stock segments). Its nut end is lowered 0.3 into the nut.
- The PegSet's locator pointers move to the model's `Locator_Peg_1..6`. `Locator_Peg_7` carries the 7th
  peg and the tuner's B letter.

The model's own textures are registered through the game's dds handler (`0x4A81D0`, NiFile adapter ctor
`0x49FF90` over an in-memory IFile) under the stock folder's names. `headstock7_wood` darkens the DXT1 end
points of the wood texture. The game shows the texture unlit, so the texture is the only brightness
control.

- **Self-build.** The `.nif` files are not shipped. At the first guitar-frame load, the mod builds
  them from the user's own installed game. `data/headstock/rsxr_headstock*.bin` is a versioned
  copy/insert program (`RSXRH1`, hex framed). The mod applies it to the game's own template NIFs, which
  it identifies by SHA-256. Unknown templates are refused, never patched heuristically. The package
  holds only authored geometry, node edits, copy ranges and hashes, plus six procedural `rsxrhs_*.dds`
  textures. Template bytes are copied at run time. The generator checks that no 16-byte run of either
  template occurs in the literals. A build takes about 0.2 s inside the loader thread. The build runs
  when a model file is missing, or when the package next to the DLL changed since the last build (an
  update). Each build writes `<package>.stamp` with the package's size and write time into the model
  folder. When the build runs, it compares existing files against the manifest hashes and rebuilds any
  that do not match. A `headstock7_file` that the player set is never replaced. If the build fails, the
  stock headstock stays. The left-handed frame
  (`epi_whilshire_cherry_lh.nif`, the right-handed model mirrored in NIF Y; CONFIRMED by comparing the
  files) has its own package and artifact `rsxr_inline7_lh.nif`. Offline regeneration:
  `tools/gen_headstock.py` (needs the maintainer's extracted templates and pipeline outputs).
- **Layouts** (`kLayouts` in `assetload_paths.cpp`): "6 in Reihe" (`guitar_standard_inline`, stock part
  `Wilshire_RH/LH`, pegs `epi_whilshire_peg.nif`) and "3+3" (`guitar_standard/gib_lespaul*.nif`, pegs
  `peg_bakelite.nif`). With 3+3, the in-line model sits correctly on the Les Paul neck. The pegs
  follow the profile option, not the frame, so the mod swaps in the in-line peg model
  (`RsxrHeadstockRead_InlinePeg`). `UI_PlayerTuner_PlacePegLetters` `0x5B1A10` would turn the letters
  of pegs >= count/2 round when the InstrumentInfo in-line byte (`+0Ah`, from profile `+13h`;
  `+8` = InvertGuitarStrings, `+9` = LeftHandGuitar) is 0. So LT1 `0x5B1B53` (and the same edit in
  the native tuner's copy) reports such a guitar as in-line (CONFIRMED in game).
- **Peg and knob tint.** The 7th peg is slot 6 of the native PegSet, so the stock peg highlight pass
  `0x543150` tints its knob like the others. The pass draws the mesh list that `Rnd_StringRenderer_Setup`
  `0x541880` collected per peg clone with NiNode `0x9E1360`. NiNode `0x9E1360` ignores the cull flag, and
  the pass never checks the flag itself (CONFIRMED by disassembly). Every 3+3 clone carries both the
  bakelite knob and `rsxr_alt`, so the hidden knob got the glow too (the wider bakelite shape). PGH1
  `0x5431DC` skips a mesh whose culled ancestor is `rsxr_alt` or one of its siblings
  (`RsxrAsset_PegMeshHidden`).
- Limits: the stock headstock shows for about 0.1 s when the pre-song tuner hands over to the song.
  With the reversed string order, the headstock strings 1-6 fade out before the posts do.
- The guitar frame carries the 8-peg model as `rsxr_alt` and the 7-peg one as `rsxr_alt5`
  (`g_file7s`, like the 5-string bass). The head shows the one the guitar is built with. The stock binder
  seats peg slot 6 on the frame's first `Locator_Peg_7`. That locator can be the hidden model's, and the
  binding can happen after our locator swap (CONFIRMED in game 2026-10-29: the B peg sat at the 8-peg
  model's post, next to E). So `KeepExtraLocators` re-points the seated extra slots to the shown model every
  shown frame.
- Tuner camera (TF2 `0x5B297F`): the stock overview framing fits six pegs. For a right-handed guitar built
  with 7/8 strings, or a 5/6-string bass, the view pans 6 units along the neck (world y, `[EBP-24h]`), so the
  outer pegs and letters stay on screen (CONFIRMED in game: the high e, and the six-string bass's high C, fell
  off the left edge). Left-handed views have the room. TF1 picks the reference string at the edge opposite
  string 0. A five-string bass's string 4 is the low B next to string 0, so TF1 uses string 3 (CONFIRMED in
  game 2026-10-31: the view sat low and co-op cut off the bass's D and G pegs).
- Song camera (SC1 `0x593A1A`, full mid-hook in `src/features/frets/hooks_camera.cpp`): all four outgoing
  camera markers follow the z shift of the highway floor. CONFIRMED by the send path `0x593980`: three z
  values are at `[ESP+18h/24h/30h]`. EDX holds the fourth z as float bits from guitar `+34h`, stored to
  `[ESP+3Ch]` after the hook. The computed pair at `0x5938A0` alone does not cover every marker.
  `rsxrfretglue::FloorOffset` is shared with the floor. It calls `FloorMove` with the actual string-0/1
  attach points and the stock local bar-B locator. It moves by `nearLanes * (a0-a1)` beyond string 0, or by
  `-farLanes * (a0-a1)` beyond the stock far string. That is 1/2 near lanes for a 7/8-string guitar, one for
  a 5/6-string bass, and one far lane for the six-string bass's high C. Handedness (`InstrumentInfo +9`) and
  inverted string order (`+8`) are independent. The attach geometry determines the side and sign. Stock
  six-string guitar, four-string bass and `g_rsxr_frets_on=0` leave the markers unchanged. Missing attach
  points or an unready bar leave this send unchanged, and the mod tries again at the next send. No guessed
  spacing is used. Only outgoing values change, so repeated sends do not add up to a growing shift.
  HYPOTHESIS pending in-game checks: following the floor restores the stock screen position of the fret
  numbers in every song/co-op camera pose.
- Unverified input: `headstock7_file` can name any custom NIF. The mod passes it to the game's NIF
  handler after a size check only (HYPOTHESIS: a malformed file can still damage the stock parser).

<details markdown="1">
<summary><b>Reference counts</b></summary>

Edited-model holders own up to 117 references (`altHold[6][9]`, `params7[9]`,
`stockHold[6][9]`). Guitar release destroys the headstock BEFORE `Rnd_GuitarStrings_Free` `0x599D70`
(CONFIRMED `0x58D9E0`, `0x592770`). NIF/DDS handler wrappers start with a zero count. Their vtable `+4`
is AddRef (`0x88A200`) and `+8` is Release (`0x8A53D0`). Calling NiObject DeleteThis on them calls AddRef.
`Rnd_SetStringShaderParams` `0x5998F0` owns nine output slots (assignment increments the LONG at `+4`,
zero calls vtable `+4`). The stock holder destructor `0x58DAB0` releases them last slot first.

</details>
