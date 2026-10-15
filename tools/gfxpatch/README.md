# gfxpatch — offline 7/8-row chord diagrams

Patches **your own copies** of Rocksmith's `RSChordDisplay` movies. This repository
contains patcher code and our extension only: no Ubisoft movies, stock class
sources, textures, encryption keys or archive decryption. It never installs
anything or launches the game. Generated sources/movies live in ignored `.work/`
and the explicitly selected output folder; do not distribute those assets.

## What is patched

**CONFIRMED (offline, 2026-10-13):** FFDec 26.3.0 successfully compiled and
re-decompiled the modified classes in the three input movies:

| Relative to `gfxassets/` | Class sprite ID | Unchanged top-level tags |
|---|---:|---:|
| `libs/commonlib.gfx` | 1103 | 2142 |
| `views/contentpanel.gfx` | 28 | 87 |
| `views/ge_game.gfx` | 26 | 85 |

The patcher replaces **one DoInitAction tag (type 59)** per movie. It uses FFDec's
compiler output only for that tag, transplanting it into the original binary.
All other tags remain byte-for-byte identical, including class registration,
imports (and authored `CommonLib.swf` spelling), exports, external DDS names,
sprites and linkage. The original GFX/SWF signature, version and frame header
are retained; declared file length changes to match the new action bytes.
For compressed movies the body is recompressed; unaffected uncompressed tags
are still identical. No new bitmap export or DDS file is needed.

The stock six `BuildString` and `UpdateString` calls, marker art and horizontal
positions are retained. Changes are:

- Add lazily built `LowBString`/`LowBNote` and `LowFsString`/`LowFsNote` clips.
  Reuse `Chord_NotePurple` for the new fretted notes and tint **those clips only**.
  Selected/unselected strips, mute and open markers reuse their stock exports.
- Infer 6/7/8 rows from the presence of the new fret properties. There is no
  `StringCount` property in this patch's contract. Missing rows are hidden,
  including finger text, when an existing component updates back to six rows.
- Keep row spacing 34 and centers `44 + 34*rank`. Row order keeps the low strings together:
  inverted (the game's usual diagram, high e on top) is `rank = 5-index` for native 0..5 and
  `rank = index` for 6/7, so the low B (and F#) rows sit **below** the low E; normal order
  (low E on top) is `rank = index + (count-6)` for 0..5 and `count-1-index` for 6/7, so they
  sit above it. Six rows are unchanged (`GfxPatchRank`). Fixed 2026-10-15: the first version
  drew the 7th row above the high e in the usual inverted view.
  Left-handed fret/marker placement remains stock, including open/mute positions.
- Grow backgrounds, gradient and nut/fret grid by `34*(count-6)`; move fret
  labels down by the same amount. Original six-row dimensions return on update.
  Background heights are 260/294/328, lesson background 285/319/353,
  gradient 190/224/258, bars 192/226/260, fret labels y=232/266/300.
- Bar vertical scale uses an absolute ratio after generic image scaling,
  preserving `yScale` without accumulating growth. The bare-name horizontal
  scaling guard gains the new bare row names. Stock uses suffixed image keys,
  so actual strips all retain the same generic `xScale` (including lesson 60%).
- Keep the five-finger hand illustration and its existing `Finger0..4` contract.

Unknown stock class sources are rejected before compilation. The exact UTF-8
FFDec 26.3.0 export hash (including CRLF) must be
`54082732b6c24bbb96632e97d6e59ab5920f4b4935be8609056c08b671978ad2`.
Already patched movies are also rejected. Inputs are hashed again after
processing to detect changes during the run.

## Exact native property contract

**CONFIRMED (research):** native serializers `0x757030` (manifest/database) and
`0x757960` (loaded SNG/lessons) currently send the stock named fields, as recorded
in the maintainer's research notes (not in this repository). This patcher changes movies
only; **the DLL must still be extended on both routes**.

**New patch-defined contract, proposed for the DLL:**

| Native index | Fret property | Finger property | RGB property | Default RGB |
|---:|---|---|---|---|
| 6 | `LowBFret` | `LowB_Finger` | `LowB_Color` | `0x808080` grey |
| 7 | `LowFsFret` | `LowFs_Finger` | `LowFs_Color` | `0x00FFFF` provisional cyan |

The eighth colour is **TBD**, not an established Rocksmith colour. Use
`--eighth-color RRGGBB` to set the compiled fallback, or send `LowFs_Color` per
shape. Supplied colour properties override fallbacks; send `LowB_Color=0x808080`
for the requested grey seventh string. RGB is a numeric `0xRRGGBB` value, not a
string or an ARGB value. These names identify native indices, regardless of tuning.

Required shape rules:

- **6 strings:** omit `LowBFret` and `LowFsFret` (or send actual AS2 `undefined`).
  Do not send `0` or `-1` to mean "absent": those draw open or muted rows.
- **7 strings:** send `LowBFret`; omit `LowFsFret`.
- **8 strings:** send both frets. An orphan `LowFsFret` without `LowBFret` is
  ignored and keeps the component at six rows. Muted extra strings still count.
- Fret properties use the **same normalized relative fret window as stock**:
  negative=mute, zero=open, positive=relative fret 1..4. Preserve `StartFret` as
  the displayed window's starting absolute fret. Compute the window over all N
  strings on the native side; this patch does not normalize absolute frets.
- Finger is optional, numeric `0..4`; `0` displays `T` (thumb). Omit absent
  fingers instead of emitting a negative sentinel. Open/muted rows have no
  finger labels. Repeated fingers/barres retain stock per-note text behavior.
- Every update must supply a **complete shape object**. Stock `BaseControl.Update`
  replaces `mDataObj`; leaving old extra keys on a reused native object would
  keep old rows enabled. Include legacy fields and handedness/inversion each time.

Legacy fields are unchanged: `LowEFret/LowE_Finger/LowE_Color`,
`AFret/A_Finger/A_Color`, `DFret/D_Finger/D_Color`, `GFret/G_Finger/G_Color`,
`BFret/B_Finger/B_Color`, `HighEFret/HighE_Finger/HighE_Color`, plus
`StartFret`, `Text`, `Visible`, `LeftHanded`, `Inverted`. `LeftHanded=1` mirrors
horizontal placement; `Inverted=1` reverses the row order over all N rows.
Optional `ShowHand` and `Finger0Fret..Finger4Fret`/`Finger0Color..Finger4Color`
remain as before. Hand fields refer to fingers, not string indices.

Example native payload additions for an eight-string shape (JSON illustration,
to merge into the complete stock object):

```json
{
  "LowBFret": 1, "LowB_Finger": 1, "LowB_Color": 8421504,
  "LowFsFret": -1, "LowFs_Color": 65535
}
```

Do not extend a native six-element pointer/stack table in place or index the
stock six-colour fallback beyond its end. The research documents those hazards
and the loaded-template resolver requirement; they are outside this repository.

## Run it

Prerequisites: `uv`, Java (tested with Temurin 21), and
[JPEXS FFDec 26.3.0](https://github.com/jindrapetrik/jpexs-decompiler/releases/tag/version26.3.0).
CLI operations follow the [FFDec CLI documentation](https://github.com/jindrapetrik/jpexs-decompiler/wiki/Commandline-arguments).
Supply an installed `ffdec.jar`; FFDec is not bundled or downloaded by this tool.

```powershell
Set-Location tools\gfxpatch
uv run python gfxpatch.py `
  --input "$env:USERPROFILE\Desktop\rocksmith-assets\static.psarc\gfxassets" `
  --output "$env:USERPROFILE\Desktop\rocksmith-assets\edited\gfx" `
  --ffdec '<path to>\ffdec.jar' `
  --java '<path to>\java.exe'
```

Input is the root containing `libs/commonlib.gfx`, `views/contentpanel.gfx`,
and `views/ge_game.gfx`, **not an archive**. Input and output roots must be
disjoint. The tool rejects game/native worktree locations and output symlinks
or junctions escaping the output root. Original input files are only read.
All three staged copies must pass before publication. Existing edited movies
require the explicit `--overwrite` flag; source inputs are never overwritten.
Keep the output `libs/` and `views/` hierarchy. Textures remain at their original
runtime asset locations; they are neither copied nor modified by this patcher.

Output movies are already generated at the command's output path. The adjacent
`gfxpatch-report.json` records hashes, changed tag indices and local evidence
paths. `.work/patch-*/` contains the exported original class, generated patched
source, re-decompiled result and FFDec logs. These intermediates contain game data and are
ignored by Git.

## Verification and limits

```powershell
uv run python -m unittest -q
node verify_layout.cjs `
  "$env:USERPROFILE\Desktop\rocksmith-assets\edited\gfx\gfxpatch-report.json" `
  '.work\layout-preview.svg'
```

Node is needed only for the optional offline drawing harness, not patching.
`verification.json` contains the final input/output hashes, changed tag indices,
test counts and evidence location from this task's audited run.
The harness executes the **re-decompiled** drawing methods with MovieClip,
TextFormat and Color stubs. **CONFIRMED in that harness:** 606 scenarios pass
across the three classes, including stock-six snapshot equivalence at init and
update, row transitions 6→7→8→6→8→7→6, handedness × inversion, 100%/60% scaling,
mute/open/frets 1..4, thumb/repeated/absent fingers, five-finger hand illustration,
default/override colours and orphan-eighth-field fallback. Binary/path unit
tests cover malformed movies, compressed transplant and unsupported sources.

The SVG is an **offline layout schematic using our own marker shapes**, obtained
from those executed methods. It is not an actual Flash/Scaleform frame; no AVM1
player or game rendering was exercised. Real bitmap appearance, parent panel
clipping, nine-slice behavior, font rendering, the game's asset loader and native
property delivery remain **HYPOTHESIS / unverified**. The extra height can exceed
an enclosing stock mask; it must be checked in chord book/popups/lesson panels
before deployment. Existing lefty behavior for relative frets >4 remains outside
scope; the DLL should keep the stock four-fret window policy.

## How edited movies could be loaded — HYPOTHESIS

**Plain PSARC override:** package the user's patched files under original archive
paths `gfxassets/libs/commonlib.gfx`, `gfxassets/views/contentpanel.gfx` and
`gfxassets/views/ge_game.gfx` in an unencrypted archive you created. The existing
plain-PSARC enabler is a possible loading route, but it does **not establish** that
an arbitrary DLC archive overrides `static.psarc` UI assets. Mount order, asset
lookup precedence, caches and when these movies load must be researched/tested.
The edited folder itself is not demonstrated to be a loose-file override.

**Runtime route:** a DLL could redirect Scaleform's movie bytes to these edited
copies before each class registration, or inject an AVM1 prototype extension after
loading. No hook or injection mechanism is implemented or verified here. All
three local class copies matter: a later movie's registration could replace an
earlier prototype. Preserve CommonLib import linkage and register-time ordering.

Neither route is deployment advice proved by this task. This task never accessed
or launched the game folder, modified the native repos, committed or pushed.
