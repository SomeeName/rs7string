# 26 tone slots: implementation notes

2026-11-17. September 2022 Steam build, image base `0x400000`, PE checksum `0x00B13D7C`.
No game installation or in-game test was performed. Builds, uv and pytest were not run, as requested by the brief.
Milestone 2 used direct clang-format and standard-library static checks; the caller must run all normal gates.

## Evidence and implementation

- **CONFIRMED (static):** milestone 1's nine patterns each match once in executable sections of the unpacked PE;
  each expected byte sequence matches at the stated address. Checked with a standard-library Python PE reader.
  These are pattern/byte checks, not the full `tools/sitecheck.py` gate or live-hook validation.
- **CONFIRMED (static evidence):** `0x56774C` exposes Attributes.Tones in EAX and its parent arrangement at `[EBP+0Ch]`;
  the arrangement identity comes from `[EBP-3A0h]` (`0x567448..0x5675EE`).
- **CONFIRMED (static evidence):** the five wrapped lookups call `0x559DB0`, stdcall(time, out id), ret 8.
  Automatic consumer `0x464ADD` has ESI=session; the other four have EBX=session.
- **CONFIRMED (static evidence):** `0x7DF400` takes stack arguments session and force, ret 8 at `0x7DF697`.
  Force=true bypasses the accessor and applies Base; its after callback restores the native timeline selection.
- **CONFIRMED (static evidence):** `0x7D3D50` takes ESI=&definition plus stack(manager, player, activation=3),
  ret 0Ch at `0x7D3F5C`. The bridge restores the C++ caller's ESI through a second full call-in after return.
  `0x465251` supplies the manager global address `0x136042C`.
- **CONFIRMED (static evidence):** manifest string unwrapping uses virtual +60h and value+8 (`0x8899F0`);
  the automatic consumer's separate JSON path uses +64h (`0x465119..0x46516C`).
  definition lookup unwraps collection entries through virtual +34h and canonical objects through +5Ch
  (`0x7D37FF..0x7D3949`). References are retained/released through virtual +0/+4.

Implementation extends the earlier worker's code. It keeps the stock registration grid unchanged, captures E..Z
in per-arrangement side tables, and owns Base/A..Z playback only with both an internal manifest marker and a
native SNG load token. The manifest rewriter generates `RsxrToneSlots=26` only for valid native attributes with
nonempty extra names; that field is reserved, not an authoring requirement. XI1 marks both native and stock
loads, with a fresh generation for reused native SNG addresses. A later non-extended manifest clears stale
capture data. Definitions are released outside the loader mutex. Two players have independent selections.
Repeated polling does not repeat decision logs; forced refreshes reapply but log only changed decisions.

## First-run silence investigation

- **CONFIRMED (code reading):** `game.cpp:Name` originally used the automatic consumer's +64h accessor for
  manifest fields. The stock manifest reader `0x8899F0` uses +60h, including the `Key` lookup from `0x7D384B`.
  The adapter and its owned JSON fixture now use that manifest contract. The regression supplies only +60h
  and checks that `Tone_E` is readable; the existing definition/apply tests use the corrected fixture too.
- **CONFIRMED (code reading):** the rewriter's `ToneMarker` preserves `Tone_E..Z` and replaces `ExtendedRange`
  with `RsxrToneSlots=26` for valid native attributes containing nonempty extra names. `Capture` used to discard
  an arrangement silently if name reading failed; `FindArrangement` also returned silently for missing manifest
  capture or a missing native SNG token. All original `tones26:` decision logs came after those gates.
- **CONFIRMED (code reading):** playback reset compared `selection.session`, which is set only after a successful
  apply. Rejections therefore reset on every poll. Playback now tracks its observed session separately, so failed
  applications retain their rejection budget as well as the once-per-load diagnostics.
- **HYPOTHESIS:** the accessor mismatch explains the first run's silence. The original log cannot establish
  the selected arrangement or which silent gate failed; audibility and live association still need a rerun.

Manifest capture logs its UUID, extra slot letters and marker, including rejected capture decisions.
XI1 now logs each SNG load and assigns a fresh generation to native and stock loads. The first consumer call
logs arrangement UUID, SNG address, player and either `extra tones 2 (E,F), active`, `no extra tones`, or a
specific rejection (feature disabled, SNG load not observed/not native, manifest not captured, missing/invalid
marker, or marked manifest with no readable extra names). The first successful timeline lookup logs
`first event raw=0 time=...`, even when extension eligibility fails. These decisions are retained independently
of successful tone application and reset for a new load, arrangement or player session. Existing applied/rejected
selection lines remain. Native tests cover inactive reasons, reloads, first-event logging and rejection spam.
Build, unit tests, pytest and the game were not run for this diagnostic change, as required by the brief.

## Second-run empty capture investigation

- **CONFIRMED (static evidence):** `[EBP+0Ch]` at `0x56774C` is the parent arrangement, not its Attributes child.
  The stock lookups at `0x5676BF` and `0x567742` use `Attributes.Tone_Base` and `Attributes.Tones`, respectively.
  Passing that parent to `Capture` made its direct `RsxrToneSlots` and `Tone_E..Z` lookups miss both fields.
  The hook now resolves `Attributes` with the stock object-member contract (`0x889CA0`, virtual +18h then +5Ch)
  before capture. The string accessor remains +60h. The earlier description of the loader argument was incorrect.
- The new hook regression supplies a parent with a nested Attributes object, checks E/F capture and native
  playback eligibility, then removes Attributes and checks that the previous capture is discarded.
- A separate manifest regression uses wholly synthetic Python `json.dumps(indent=2, ensure_ascii=False)` text
  with the demo's nesting and relevant key order, six tone definitions, nonempty E/F and empty G..Z. It compares
  all output bytes against the expected marker/tuning replacements for Lead and Rhythm, and checks that the
  unmarked form is left untouched. The production manifest rewriter is unchanged.
- **HYPOTHESIS:** correcting the parent/child mismatch resolves the second run's `extra tones=0 ... marker=0`.
  The regression tests have been added but were not executed: builds, uv and pytest are forbidden by this brief.
  The caller still needs the native test run and an in-game rerun to establish audible E/F changes.

## Native registration

2026-11-17, milestone 2 static investigation. Standard-library Python parsed the PE sections and checked call
targets/patterns; LLVM decoded selected code ranges from a temporary copy with its malformed TLS-directory size
corrected for the decoder. No game bytes or listings were saved in this checkout.

- **CONFIRMED (`0x7D1510..0x7D1550`, `0x5A8700`):** the manager allocates a `0x14`-byte resource-store object
  at `+0x1A4`. Its outer vector is at `+8/+0Ch/+10h`. Construction calls `0x5A8AA0`, which sizes it to
  **18 records** (`0x5A8ABE`, `0x5A8AF2`), each 12 bytes, for `0xD8` bytes of initial record storage.
  Allocation goes through `0x5A8BA0 -> 0x8FB9C0` (`0x5A8BE6`). Each record is itself a begin/end/capacity
  vector of reference-counted resource pointers, not a tone-definition pointer.
- **CONFIRMED (`0x5A87B4..0x5A87E3`, `0x7D43D6..0x7D4404`):** records 0..7 are a different four-per-player
  resource-registration path. `0x7D6D09..0x7D6D38` writes song Base/A..D into records 8..17 with stride five.
  Both paths call resource-vector builder `0x5A70A0`.
- **CONFIRMED (`0x5A70D9..0x5A737F`):** the builder reads GearList, visits fourteen gear-category names,
  constructs `0xB0`-byte resource objects and calls `0x5A7AF0`. That routine looks up resource definitions and
  either attaches existing resources (`0x5A7D43..0x5A7D65`) or queues asynchronous loading with a resource-object
  callback (`0x5A7DA7..0x5A7E22`). Registration therefore requests/retains gear resources; return does not prove
  that asynchronous loading has finished, or that a sound has been activated.
- **CONFIRMED (`0x5A8800`, caller `0x7C0839`):** whole-store clearing walks the actual outer-vector length.
  **CONFIRMED (`0x5A8900`, callers `0x65D91D`, `0x73C4AD`):** another clear path hard-codes record byte ranges
  `0x30..0x60` and `0x9C..0xD8`. **CONFIRMED (`0x5A8A00`, caller `0x7DD5DC`):** song-resource clearing
  hard-codes two groups of five starting at byte `0x60`. These paths release resources and reset each end pointer.
- **CONFIRMED (`0x5A8750`, `0x5A8EF0`):** store destruction destroys the nested vectors and frees their game
  allocations. `0x5A8EF0` is cdecl with stack arguments (record end, record begin), walking backwards by 12.
- **CONFIRMED (`0x7D6CE0..0x7D6D62`):** stock registration takes EAX=player and stack(manager, definition,
  slot), returns with `ret 0Ch`, and consumes one caller-owned definition reference. It has no boolean result.
- **HYPOTHESIS:** all surviving aliases/readers of the shared store have been identified. The mapped direct
  writers and cleanup paths do not constitute an exhaustive pointer-flow proof, including asynchronous resource
  callbacks and other manager users. Tone Plan and activation do not become extensible merely by enlarging this store.

### Design and implementation scope

The preferred replacement of the live manager store is blocked by that incomplete alias/lifetime map and the
fixed cleanup partitions. Keep the stock store, strides, bounds, activation and Tone Plan unchanged. Implement
only isolated E..Z pre-registration: a mod-owned 62-record buffer (eight reserved plus two rows of 27), with
short-lived manager/store views passed to the **unmodified** stock registration call. For logical native slot
`s` (Base=0, A=1, E=5, Z=26), a view begins at record `player*22+s`; stock `(player, slot=0)` then addresses
record `8+player*27+s`. The original manager is never exchanged, mutated or published to another thread.
Only resource objects, not these temporary views, escape the synchronous stock routine.

Run after stock session registration, gated by the existing native SNG and manifest checks; if that hook occurs
before eligibility is available, the first eligible playback lookup makes the same bounded attempt. Keep resources
per observed player/load and release them through the stock nested-vector destructor on replacement. Log one
decision for every declared extra slot per attempt; `ok` means a well-formed resource vector was constructed,
including an empty vector for gearless definitions. It does not mean preload completion. Missing definitions or
unavailable registration infrastructure reject that slot, and milestone 1 still applies its normal on-demand path.
Playback continues using full-object apply even after successful resource registration, just as stock registration
does not replace activation. Complete first-class reader integration remains deferred, not claimed by this change.

Implementation/check plan: add owned ABI/mapping/cleanup tests first; add the isolated store and EAX call-in;
wire a full-mode registration wrap and two call references in the tones failure group and emulator tables;
connect per-load registration state; check patterns/expected bytes and source hygiene with plain Python/Git.
Build, uv, pytest and in-game gates are explicitly delegated to the caller by this brief.

**CONFIRMED (source inspection):** `native_registration.{h,cpp}` owns the isolated vectors and bridges the
stock EAX ABI. `state.cpp` attempts each declared extra slot once per eligible observed player/load, after stock
registration or at the first eligible lookup. The original timeline/full-object apply path still handles every
selection, including rejected registration. Native tests cover the real call-in ABI with an owned stand-in,
E/Z endpoints for both players, reference consumption, unavailable cleanup, native gating, repeated setup,
reload cleanup and bounded rejection logs. The emulator test adds inactive registration-wrap preservation.
These tests are written but **unexecuted** under the brief's restrictions.

**CONFIRMED (static checks):** all three milestone-2 patterns below match exactly once at their stated sites,
and expected bytes/call targets agree. Direct clang-format dry-run and `git diff --check` passed; changed Python
files parse with `ast`, local includes and documentation file links resolve, and touched files remain below
800 lines (`emu_harness.py`: 763). Full formatting/layout gates require the caller's dependency environment.
The owned cleanup fixture now populates resource vectors and checks the absolute first record and one release
per retained synthetic resource, in addition to relative player/slot spacing. A read-only code review found no
concrete ABI/indexing defect within this isolated contract; it does not validate the game's asynchronous behavior.

In game, expect exactly one `tones26: register player=0 slot=5 key='...' ok` for E and slot 6 for F per
observed load (player 1 has separate decisions). A missing/duplicate Key must instead show
`rejected: missing or ambiguous definition`; an unavailable adapter shows `rejected: registration unavailable or failed`.
Existing `id=4`/`id=5 ... applied` logs must still follow timeline selections. Seeking, polling and forced refresh
must not repeat registration decisions. Check native -> stock -> native and co-op for resource lifetime/aliasing.

**HYPOTHESIS:** isolated registration requests finish early enough to avoid first-use asset stalls. Live resource
readiness, async-callback lifetime, resource sharing between slots/players, and teardown still need game validation.
A trapped fault in stock registration has ambiguous by-value reference ownership; it is rejected without retrying
that decision, and may retain a reference. Resources are released on the next observed session/load/eligibility
change (or shutdown), not immediately on song exit. Full store-reader redirection and Tone Plan remain blocked/deferred.

## Tone Plan editor

2026-11-17, milestone 3 static investigation. Plain Python and the standard library parsed the unpacked PE
section table and inspected instruction operands, direct calls, strings and references. The image checksum is
`0x00B13D7C`. No game listings or extracted assets are retained in this checkout. The original design-only
investigation is preserved below; the subsequent implementation and its remaining limits are recorded under
[Demo-first editor implementation](#demo-first-editor-implementation).

### Screen, rows and data sources

- **CONFIRMED (`0x5D0C07`, string `0x11B5230`; constructor `0x6498D0`):** this menu is registered as
  `LAS_SongOptionsTonePlan`; its movie name is `UIMenu_LAS_SongOptions_TonePlan` (`0x11B5248`). Its page
  vtable is `0x11B5390`. The builder at vtable `+0xB0` is `0x64B980`; its select handler at `+0xD8` is
  `0x64BBE0`. Do not confuse it with the destination tone chooser, `LearnASong_SongOptions_Tone`
  (`0x64BC97`), whose builder is `0x648850` and whose selection/update path includes `0x649210`.
- **CONFIRMED (`0x64BB29..0x64BB96`, `0x625B86..0x625C62`):** the builder obtains the selected arrangement's
  **Attributes object**, through `0x625960`, and passes page and Attributes as the two stack arguments to
  `0x649A00` and `0x64AC30`. Both helpers return with `ret 8`. This differs from `ToneLoad`, which receives
  the parent arrangement. `PersistentID` is read directly from Attributes at `0x64AC87`.
- **CONFIRMED (`0x649B56..0x649C73`):** `0x649A00` builds two local five-string tables, each with `0x18`-byte
  string objects: `ToneBase`, `ToneA`..`ToneD` at working ESP `+0x1C0`, and `Tone_Base`, `Tone_A`..`Tone_D`
  at working ESP `+0x148`. The stock strings are constructed and destroyed locally; they cannot be replaced
  by placing 27 objects on the existing stack. The row index is at working ESP `+0x54`; its byte offset
  (`index * 0x18`) is saved at `+0x34`. ESP here means the stable body ESP after the three register pushes,
  outside temporary call arguments.
- **CONFIRMED (`0x649D12..0x649D71`, `0x649E60..0x649EB8`, `0x649F58`):** each nonempty manifest `Tone_*`
  name supplies a definition Key. The builder constructs `LAS_DefaultToneKeys` and `LAS_DefaultToneMappings`
  in the selected player's `CustomTone` context. It searches the supplied `Tones` collection by its actual
  length (`0x64A104..0x64A385`), reading definition `Key` and `Name` (`0x64A18A`, `0x64A1CE`). A matching
  definition supplies the default display name. There is no access here to the manager's registration vectors.
- **CONFIRMED (`0x64AB5B..0x64AB67`):** the outer row loop increments its logical slot index, compares it
  with **5** at `0x64AB64`, and loops to `0x649CA5`. Empty slot names take the skip/cleanup path
  (`0x649DF0..0x649E16`). Thus the number of emitted buttons is not necessarily the outer loop bound.
- **CONFIRMED (`0x64A483..0x64A529`, `0x64A550..0x64A8DC`):** a row becomes `toneButton%i` below page
  `+0x1C4`. Its fields include `DefaultToneName`, `Label`, `ToneNameExtension` (`ToneBase` / `ToneA`..),
  `ToneKey`, `ID`, `SortOrder` and `States.Default`. The ID is the button name; the profile override property
  is separately formatted as `LAS_ToneCustomData_%s`. An existing custom object's `Name` overrides the Label
  (`0x64A5B9..0x64A61A`). IDs are also appended to the string vectors at page `+0x214` and `+0x220`
  (`0x64A94C`, `0x64AA69`). These are not evidence of an unlimited Flash viewport or working scrolling.

### Selecting, editing and the timeline graphic

- **CONFIRMED (`0x64BD48..0x64BE62`, `0x64BFB5..0x64C0EC`):** selection obtains the focused button with
  `0x5DAF00`, reads its `ToneNameExtension` and `ToneKey`, and prepares `LAS_ToneNameBeingEdited` plus
  `LAS_TempDefaultToneName` in the player's context. It walks `LAS_DefaultToneKeys` by collection length;
  entries containing the selected Key add `ToneExtension` records to the edit list. The destination state
  passed to `0x76DD90` is `LearnASong_SongOptions_Tone` (`0x64C18E..0x64C1AD`). This path carries strings,
  rather than a switch on five numeric tone IDs.
- **CONFIRMED (`0x6492B3..0x649303`, `0x64938F..0x649483`):** the chooser's update path walks the edit list,
  reads each `ToneExtension`, formats `LAS_ToneCustomData_<extension>`, removes the previous member, and
  obtains the replacement from the selected custom tone (`0x649630`). It copies that replacement into the
  player's context. `0x649630` reads `CustomToneName` from the focused chooser entry and searches the available
  tone collection by Key (`0x6496C8..0x649813`). This establishes a context update, **not** durable per-song
  save/reload behavior for E..Z. The `EditTonePlan` notification at `0x6494CD` calls the mission/achievement
  path `0x4C5900`; it must not be assumed to be a profile persistence API.
- **CONFIRMED (`0x64AE1F..0x64AF03`, `0x64B03F..0x64B0D8`):** `0x64AC30` resolves the arrangement resource
  by PersistentID through `0x568A50` / `0x572840`, obtains its song object at resource `+8`, and scans the song's
  `+0xD0` vector in **0x104-byte records**. It matches the record's text at `+4` against `tone_base` and
  `tone_a`..`tone_d`. This is separate from numeric tone changes at song `+0xDC`; widening the numeric timeline
  does not widen this graphic.
- **CONFIRMED (`0x64AF09..0x64AFE5`):** the graphic's ten stack strings are **five interleaved pairs** with
  pair stride `0x30`: event label followed by `ToneBase` / `ToneA`..`ToneD`. They are not two contiguous
  five-string tables. The match-loop bound is 5 at `0x64B0C7`, with a second unmatched-label bound at
  `0x64B0D1`. Name-table reads occur at `0x64B1D0` and `0x64B56F`; the latter is the final-block path.
  Output goes to `ToneBlockObject.DataArray` with `ToneName`, `BlockStart` and `BlockEnd`
  (`0x64AFFB`, `0x64B23D`, `0x64B2A2`, `0x64B35E`, `0x64B5D2`).
- **CONFIRMED (source inspection, `state.cpp:Object` / `Handle`):** native playback applies the captured
  manifest E..Z objects directly. It does not consult the player's `LAS_ToneCustomData_ToneE`..`ToneZ`
  context members. Eligible native Base/A..D also use the mod's direct-object path. Therefore displaying extra
  buttons and letting the chooser update context would still leave audible playback ignoring those edits.

### Native route and implementation blockers

**HYPOTHESIS / proposed design:** own 27 rows per editor instance, identified by player, arrangement UUID,
native SNG generation and page lifetime. Include Base plus the declared nonempty A..Z names, preserving logical
letters across holes and duplicate Keys. Keep logical slot IDs separate from visible button IDs. Confirm the
resource's song `+8` against the XI1 native-load token, plus the existing captured manifest marker; a menu must
not borrow the last gameplay session's eligibility. Retain any referenced game objects for the build/edit
transaction and release them outside the loader mutex.

Redirect both row-table reads and the loop bound through full mid-hooks behind `kRsxrGateTones`. Leave the
original five stack objects and their destructors intact; the game must never destroy mod-owned string views.
Supply all logical mappings to `LAS_DefaultToneKeys` / `LAS_DefaultToneMappings`, including hidden rows.
Extend the graphic's paired event/name source, both bounds and both name reads together. Its separate event
labels must be reconciled with native numeric E..Z changes; do not fabricate or mutate the game's SNG vector.

**HYPOTHESIS / proposed paging:** use five physical `toneButton0`..`toneButton4` entries if the movie cannot
scroll a longer list. Adapt `arrangements/hooks.cpp`'s focus-edge Up/Down and PageUp/PageDown window movement,
root rebuilding and focus-list swapping. Keep each physical button's `ToneNameExtension`, `ToneKey` and override
property tied to the logical row; remap the `%i` button argument only, not the logical mapping arrays. Preserve
`States` across rebuilding and restore focus before a select can run. Do not reuse the arrangement page's
vtable, record offsets or UI state string. Log once on a real editor entry, not on each window rebuild:
`tones26: toneplan rows=7 (Base,A..F)` for the six-tone demo; `rows=27 (Base,A..Z)` at capacity. Sparse-slot
logs should list the actual letters instead of implying a contiguous range.

**Historical investigation stop (before implementation):** the display-table contract is mapped, but the complete edit-to-playback contract is
not. Two pieces remain unestablished: (1) how the updated `CustomTone` context is saved/restored for the same
song/arrangement and player, including reset-to-default and shared default Keys; (2) the Tone Plan movie's
viewport, button removal/redraw and focus-state behavior during a rebuild. The executable's dynamic button
construction alone proves neither. The arrangement implementation documents movie-specific redraw behavior;
it cannot safely be copied to this different movie without establishing those contracts. The known native
playback bypass above must also be changed with explicit custom-override precedence and invalidation when the
chosen definition changes without a slot-ID change. Until then, a table-only patch fails the requested picking
and editing behavior even if its rows render. This is an unresolved integration blocker, not proof that native
extension is impossible. No production hooks, emulator entries, unit tests or user-facing completion claims
were added. The changelog's existing limitation is deliberately retained.

To resume: trace the `CustomTone` context's per-song transfer on leaving/returning to this menu and native
session setup; establish an owned/reference-safe override resolver for `state.cpp:Object`, including Base/A..D;
then validate the five-button rebuild on the actual Tone Plan movie. Only after those contracts are known,
implement the table/count/paging hooks together and add tests for stock fallback, E/Z edits and reset, duplicate
Keys, sparse slots, same-slot replacement, native/stock/native, reopen and two players. The expected test order
is owned ABI/model tests, inactive emulator preservation, pattern/expected-byte checks, then caller build and
in-game gates. These tests have not been written or run in this design-only change.

### Candidate anchors (not installed)

**CONFIRMED (standard-library static scan):** each complete pattern below matches once in executable PE
sections at the stated address. These are research anchors, not entries in `sites.json` and not a complete
hook set. Bytes are shown solely as prospective short signatures. Longer signatures are needed for the two
graphic name reads: their identical 13-byte sequences match **both** `0x64B1D0` and `0x64B56F`.

| Purpose | Expected address | Unique anchor pattern | Initial complete instruction bytes |
|---|---|---|---|
| Row-builder call | `0x64BB4C` | `E8 AF DE FF FF 85 C0 75 33 8B 16 8B 42` | `E8 AF DE FF FF` |
| Manifest-field string source | `0x649D12` | `8D BC 04 48 01 00 00 C6 84 24 30 01 00 00 00` | `8D BC 04 48 01 00 00` |
| Slot-extension string source | `0x649E60` | `8D 84 04 C0 01 00 00 8D 48 10 89 44 24 44` | `8D 84 04 C0 01 00 00` |
| Row bound and loop branch | `0x64AB64` | `83 F8 05 0F 8C 38 F1 FF FF` | `83 F8 05 0F 8C 38 F1 FF FF` |
| Graphic paired-table source | `0x64B07A` | `8D B5 18 FF FF FF 8B 95 94 FE FF FF 8B 7E 04` | `8D B5 18 FF FF FF` |
| Graphic match bound | `0x64B0C7` | `83 F8 05 72 B4 E9 AD 03 00 00` | `83 F8 05 72 B4` |
| Graphic unmatched-label bound | `0x64B0D1` | `83 BD A8 FE FF FF 05 0F 83 A0 03 00 00` | `83 BD A8 FE FF FF 05` |

**HYPOTHESIS (player navigation; not tested live here):** Learn a Song -> choose the native demo song and
arrangement -> Tone Plan -> select a tone row -> the stock tone chooser. If navigation differs, confirm by
identifying the movie above and the destination `LearnASong_SongOptions_Tone`; do not substitute the F9 overlay
or the general Tone Designer. The exact string returned by `RsxrUi_ReadState` for the Tone Plan page has not
been established here; the registration name must not be used as an input gate without checking it. No
`tones26: toneplan` line is emitted by the current build: the lines above are acceptance targets for the future
implementation.

**CONFIRMED (handoff checks):** all seven candidate anchors above were checked again for unique executable-section
matches, exact initial bytes and complete instruction spans. Relative documentation file links resolve and
`git diff --check` passes. These checks do not validate movie behavior, profile persistence or live hooking.
Builds, uv, pytest, MkDocs and in-game tests were not run; the brief delegates the executable gates to the caller.

### Demo-first editor implementation

2026-11-17, milestone 3 part 2. Implementation is saved but has not been built or executed in the game.
The brief forbids build, uv and pytest; native/emulator tests are added for the caller to run.

- **CONFIRMED (static, `0x64BB4C`, `0x649D12`, `0x649E60`, `0x64AB64`):** the row-builder wrap passes the
  current Attributes object to `PlanSlots`. Only the internal native manifest marker `RsxrToneSlots=26` plus
  a nonempty E..Z field enables its 27-slot loop. There is no dependence on the last gameplay session. The two
  name reads use mod-owned, stable `0x18`-byte game-string views for Base/A..Z and `Tone_Base`/`Tone_A`..`Tone_Z`.
  All five original stack strings in each table and their destructors remain untouched. The stock builder skips
  empty names and creates every row, mapping, button ID, SortOrder and focus-vector entry itself. Nested builder
  calls restore the outer mask; unmarked and marked-without-extras arrangements replay the stock instructions.
- **CONFIRMED (static movie inspection; `0x6498D0` identifies the movie):** the user's extracted
  `las_songoptionstoneplan.gfx` has SHA256
  `46761d713d3dcdcd090fea5b56ed863b6f5c596398d125fad3c3e3b7f3ed2d03`.
  DoInitAction sprite 25's `SongOptionsTonePlanScene.InitButtons` calls its parent. Sprite 22's
  `SongOptionsScene2.PopulateButtons` loops over `mSortedData.length`, creates/updates each button by its ID
  and sets it visible; there is **no fixed five-button set**. Its `BuildButtonDataObject` computes Y from
  `270 + (SortOrder + 1) * 35`, with font size 30. Thus arbitrary counts are instantiated, but the layout
  does not fit 27 rows in the viewport. No paging, scrolling, movie mutation or row compaction is implemented.
  **HYPOTHESIS:** E/F in the six-tone demo remain visible and selectable with stock layout; full plans and sparse
  high-letter plans overflow the viewport and need a later layout change. Live focus/rendering is untested.
- **CONFIRMED (static, `0x64BE44..0x64C1AD`, `0x6492B3..0x649483`):** the existing selection handler reads
  the supplied extension/Key, builds `LAS_ToneNameBeingEdited` using `LAS_DefaultToneKeys`, and opens the stock
  chooser. Its update loop writes `LAS_ToneCustomData_ToneE`..`ToneZ` using the same object/property format as
  A..D. `TonePlanEdit` only logs this decision; it does not replace the string-based selection/edit code.
  Shared default Keys retain the game's behavior of editing all matching extensions.
- **CONFIRMED (static, `0x464BAA..0x464C05`, `0x465259`, `0x41B310..0x41B498`):** automatic stock A playback
  resolves `CustomTone/<decimal player>/LAS_ToneCustomData_ToneA` through virtual `+18h` then `+5Ch`, with the
  manifest object as fallback, and passes the chosen object to full apply `0x7D3D50`. Registration also reads
  this property (`0x7E9B7E..0x7E9C17`); Base uses the corresponding `ToneBase` property (`0x7DEED8..0x7DEF46`).
  `game.cpp:Override` now performs that same retained lookup for all 27 slots, only after native playback
  eligibility succeeds. `state.cpp:Handle` detects same-slot object replacement or removal, retains the last
  applied custom object to prevent dangling identity comparisons, and restores the manifest default on removal.
  Stock-only sessions return to stock before the new lookup. References are released outside the loader mutex.
- **HYPOTHESIS:** the game's context transfer saves/restores E..Z across leaving/reopening an arrangement,
  changing songs and restarting the game exactly as it does A..D. This change uses the stock chooser's property
  writer and stock playback's property consumer; it adds no separate persistence store. Durable profile behavior,
  chooser reset and co-op must be checked live. The `0x64AC30` timeline graphic still reads its stock textual
  five-pair table; native numeric E..Z events are not added to that graphic by this demo change.

Decision logs (once per editor build, accepted extra-slot selection, or changed playback selection/override):

```text
tones26: toneplan rows=7 (Base,A,B,C,D,E,F used)
tones26: toneplan edit slot=E key='...' stock chooser
tones26: override slot=E key='...' applied
tones26: override slot=E key='...' none
```

The last line means no custom object was applied (default fallback or rejected apply). Repeated polling and
forced refresh of an unchanged successful selection do not repeat override decisions. Missing/invalid IDs
retain the existing bounded rejection logs. A same-Key replacement still reapplies because its object changed.

All six new patterns below matched exactly once in executable PE sections; expected bytes matched, and
Capstone confirmed complete instruction spans. This is a direct static check, not the full sitecheck gate.
References are never written. All six belong to the existing optional Tones failure group.

| ID | Expected address | Pattern | Expected bytes |
|---|---|---|---|
| `TonePlanBuild` | `0x64BB4C` | `E8 AF DE FF FF 85 C0 75 33 8B 16 8B 42` | `E8 AF DE FF FF` |
| `TonePlanField` | `0x649D12` | `8D BC 04 48 01 00 00 C6 84 24 30 01 00 00 00` | `8D BC 04 48 01 00 00` |
| `TonePlanExtension` | `0x649E60` | `8D 84 04 C0 01 00 00 8D 48 10 89 44 24 44` | `8D 84 04 C0 01 00 00` |
| `TonePlanBound` | `0x64AB64` | `83 F8 05 0F 8C 38 F1 FF FF` | `83 F8 05 0F 8C 38 F1 FF FF` |
| `TonePlanEdit` | `0x64BE44` | `8D 4C 24 0C 51 B9 D8 50 1B 01 E8 1D 06 23 00` | `8D 4C 24 0C 51` |
| `ToneContext` | `0x464B9B` | `8B 0D CC 04 36 01 8B 49 14 50 8D 54 24 24` (offset 2) | `CC 04 36 01` |

Native tests cover name-view layout, sparse E/Z, invalid/missing marker, no extras, disabled feature, source/bound
redirection, nested stock fallback, per-player retained context lookup, repeated polling, same-Key replacement,
removal/default reset, Base/A overrides and stock transition cleanup. Emulator tests add inactive row sites and
the builder's argument/result/stack preservation. The reference/export lists and registry ref count are updated;
`emu_harness.py` remains below 800 lines. Caller gates and the six-tone demo are still required.

**CONFIRMED (static verification):** direct clang-format, the existing blank-line/layout tools and Ruff passed;
changed Python files parse, relative documentation file links resolve, and `git diff --check` passes.
`emu_harness.py` has 766 lines. The native and emulator tests were not executed. No build, uv, pytest, game-folder
write, asset export, generated-header edit or commit was performed.

## Sites

The full patterns and exact displaced bytes are also in `sites.json`; references are never written.

| ID | Expected site | Kind | Pattern | Expected bytes |
|---|---|---|---|---|
| `ToneRegister` | `0x7E9AF0` | `jmp` | `55 8B EC 83 EC 08 53 8B 5D 08 80 BB A6 05 00 00 01 56 57 0F 84 4F 03 00 00` | `55 8B EC 83 EC 08` |
| `ToneRegisterCall` | `0x7E9B69` | `callref` | `E8 72 D1 FE FF 8B 4D FC 85 C9 0F 84 E8 02 00 00` | `E8 72 D1 FE FF` |
| `ToneResourceDestroy` | `0x5A876D` | `callref` | `E8 7E 07 00 00 83 C4 08 8D 46 08 E8 E3 3A 2B 00` | `E8 7E 07 00 00` |
| `ToneLoad` | `0x56774C` | `jmp` | `89 85 5C FC FF FF 85 C0 0F 84 76 01 00 00 8B C8` | `89 85 5C FC FF FF` |
| `ToneAuto` | `0x464ADD` | `call` | `E8 ?? ?? ?? ?? 84 C0 0F 94 C3 85 FF 74 17 8D 47` | `E8 CE 52 0F 00` |
| `ToneSetup` | `0x7DF1CF` | `call` | `E8 DC AB D7 FF 84 C0 74 0B C6 44 24 13 01 EB 09` | `E8 DC AB D7 FF` |
| `ToneSeek` | `0x7DF485` | `call` | `E8 26 A9 D7 FF 84 C0 74 0F C6 44 24 13 01 EB 0D` | `E8 26 A9 D7 FF` |
| `ToneResume` | `0x7ECAB7` | `call` | `E8 F4 D2 D6 FF 84 C0 74 0B C6 44 24 13 01 EB 09` | `E8 F4 D2 D6 FF` |
| `ToneOverride` | `0x474BCE` | `call` | `E8 DD 51 0E 00 84 C0 74 0B C6 44 24 13 01 EB 09` | `E8 DD 51 0E 00` |
| `ToneRefresh` | `0x7DF400` | `jmp` | `55 8B EC 83 E4 F8 83 EC 34 A1 ?? ?? ?? ?? 33 C4 89 44 24 30 8A 45 0C 53 8B 5D 08 56 C7 44 24 10 00 00 00 00 80 BB 44 05 00 00 01 57` | `55 8B EC 83 E4 F8` |
| `ToneApply` | `0x465259` | `callref` | `8B 45 08 8B 40 0C 8B 40 08 8B 0D ?? ?? ?? ?? 6A 03 50 51 E8 ?? ?? ?? ??` | `E8 F2 EA 36 00` |
| `ToneManager` | `0x465251` | `ref` | `8B 45 08 8B 40 0C 8B 40 08 8B 0D ?? ?? ?? ?? 6A 03 50 51 E8 ?? ?? ?? ??` | `2C 04 36 01` |

## Caller gates and live demo

Run `scripts/format.ps1`, `build.ps1`, `uv run pytest tools/tests -q`, `uv run python tools/sitecheck.py`,
`uv run python tools/check_exports.py bin/Release/dinput8.dll`, and the strict MkDocs build.
New native tests cover slot selection, optional-group isolation, the actual apply bridge ABI with an owned
stand-in, duplicate Key rejection, manifest/SNG gating, stale manifest replacement, independent player objects,
native reload generations, and the automatic callback's F/sentinel suppression. Python tests cover
native/stock/native timeline preservation and the six-tone demo's output. They have not been executed here.

Standard-library static checks also parsed the three changed Python files, checked changed-source whitespace
and local include paths, checked the edited documentation's relative file links, and counted the touched
manifest helpers below 100 source lines. `git diff --check` passed. These do not replace the caller gates.

Generate the demo using `tools/tone_demo.py` with an author-owned native package and six distinct, complete
existing Keys. It writes A..F and 20 chronological changes; it does not generate Ubisoft assets or definitions.
The caller must hear six different sounds and inspect lines of the form
`tones26: arrangement=... player=0 id=4 key='...' applied` and the corresponding `id=5` line.
Before the first event expect `id=26` (Base). Check all A..F transitions, seek before/on/after E and F,
Riff Repeater rewind, restart, pause/tuner/resume, two players with different definitions, and leaving the native
song for stock content. Missing/duplicate Keys and invalid IDs must log `rejected` without switching sounds.

## Open risks and follow-ups

- **HYPOTHESIS:** the runtime JSON lifetime and apply ABI behave as the static evidence indicates; live execution,
  audibility, asset availability/preload, stack/register preservation and co-op still require the owner's demo.
- **HYPOTHESIS:** the five accessor callers plus forced refresh cover all practical seek/restart/pause paths.
- Native sessions with extra slots now prefer the stock per-player Tone Plan custom objects, including A..D/Base.
  Durable profile transfer, emulated-bass/multiplayer precedence, and long-plan layout remain follow-ups;
  stock-only sessions keep stock code.
- Retained manifest objects are cached per arrangement until replacement or shutdown. SNG tokens are removed on
  address reuse, not at destruction; bounded cache eviction/destructor cleanup is a hardening follow-up.
- Native eligibility follows the independently validated native manifest and SNXR body; no new archive-owner
  association is introduced. A manually assembled PSAR containing both native payloads is not distinguished.
- **CONFIRMED (2026-11-17, M3 build, owner's game):** the stock Tone Plan editor has no visible entry on PC
  Remastered. The Learn a Song song page (arrow keys move nothing; Ctrl opens Role/Input), the in-song pause menu
  (resume, restart, tuner, mixer, chords, quit), Riff Repeater and its advanced settings offer no tone item. In the
  exe, the only reference to the `LearnASong_SongOptions_Tone` state is the Tone Plan select handler `0x64BBE0`
  (vtable slot at `0x11B5468`), and `EditTonePlan` is referenced only from the chooser (`0x649210`). The menus open
  each other; nothing found opens the first one. **HYPOTHESIS:** these menus are console-era leftovers that the
  Remastered movies never open. So the editor rows/edit/override code is untested in game. A player-facing tone
  edit would need our own entry (pushing that state) or a Sound Designer route.
- Run log the same day: the 6-tone chart still registers E,F (`register player=0 slot=5 key='Cleanalt' ok`,
  `slot=6 key='Mod' ok`); the override path logs `override slot=Base key='Clean' none` and
  `override slot=A key='Clean' none` (no stored override, stock chooser unreachable).
