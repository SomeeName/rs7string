# gfxpatch — offline 7/8-row chord diagrams

This tool patches **your own copies** of Rocksmith's `RSChordDisplay` movies so that chord diagrams can show
seven or eight rows. This repository contains only the patcher code and our extension. It has no Ubisoft movies,
stock class sources, textures, encryption keys or archive decryption. The tool never installs anything or launches
the game. Generated sources and movies go into the ignored `.work/` folder and into the output folder you select.
Do not distribute those assets.

## What is patched

**CONFIRMED (offline, 2026-10-13):** FFDec 26.3.0 successfully compiled and
re-decompiled the modified classes in the three input movies:

| Relative to `gfxassets/` | Class sprite ID | Unchanged top-level tags |
|---|---:|---:|
| `libs/commonlib.gfx` | 1103 | 2142 |
| `views/contentpanel.gfx` | 28 | 87 |
| `views/ge_game.gfx` | 26 | 85 |

The patcher replaces **one DoInitAction tag (type 59)** per movie. It uses FFDec's
compiler output only for that tag and transplants it into the original binary.
All other tags stay byte-for-byte identical. That goes for class registration,
imports with the authored `CommonLib.swf` spelling, exports, external DDS names,
sprites and linkage. The original GFX/SWF signature, version and frame header
stay the same. The declared file length changes to match the new action bytes.
For compressed movies, the body is recompressed, and the unaffected uncompressed tags
are still identical. No new bitmap export or DDS file is needed.

The stock six `BuildString` and `UpdateString` calls, the marker art and the horizontal
positions stay as they are. The patch makes these changes:

- It adds `LowBString`/`LowBNote` and `LowFsString`/`LowFsNote` clips, built only when first needed.
  It reuses `Chord_NotePurple` for the new fretted notes and tints **those clips only**.
  Selected/unselected strips, mute and open markers reuse their stock exports.
- It infers 6/7/8 rows from whether the new fret properties are present. There is no
  `StringCount` property in this patch's contract. When an existing component updates back to six rows,
  the missing rows are hidden, including their finger text.
- It keeps the row spacing of 34 and the row centers at `44 + 34*rank`. The row order keeps the low strings together:
  - Inverted order (the game's usual diagram, high e on top): `rank = 5-index` for native 0..5 and
    `rank = index` for 6/7. The low B (and F#) rows sit **below** the low E.
  - Normal order (low E on top): `rank = index + (count-6)` for 0..5 and `count-1-index` for 6/7. The new rows
    sit above the low E.
  - Six rows are unchanged (`GfxPatchRank`). Fixed 2026-10-15: the first version
    drew the 7th row above the high e in the usual inverted view.
  - Left-handed fret/marker placement stays stock, including open/mute positions.
- It grows the backgrounds, the gradient and the nut/fret grid by `34*(count-6)`, and moves the fret
  labels down by the same amount. The original six-row dimensions return on update.
  Background heights are 260/294/328, lesson background 285/319/353,
  gradient 190/224/258, bars 192/226/260, fret labels y=232/266/300.
- The vertical scale of the bars uses an absolute ratio after the generic image scaling.
  This keeps `yScale` without adding growth on each update. The horizontal scaling guard for bare names
  gains the new bare row names. Stock uses suffixed image keys, so the actual strips all keep the same
  generic `xScale` (including lesson 60%).
- It keeps the five-finger hand illustration and its existing `Finger0..4` contract.

The patcher rejects unknown stock class sources before compilation. The exact UTF-8
FFDec 26.3.0 export hash (including CRLF) must be
`54082732b6c24bbb96632e97d6e59ab5920f4b4935be8609056c08b671978ad2`.
It also rejects movies that are already patched. It hashes the inputs again after
processing, to detect changes during the run.

## Exact native property contract

**CONFIRMED (research):** the native serializers `0x757030` (manifest/database) and
`0x757960` (loaded SNG/lessons) currently send the stock named fields. The maintainer's research notes record
this (they are not in this repository). This patcher changes only the movies. The DLL supplies the new fields on
both routes (the `DGM` and `DGS` hooks in `src/features/diagrams/`).

**New patch-defined contract, proposed for the DLL:**

| Native index | Fret property | Finger property | RGB property | Default RGB |
|---:|---|---|---|---|
| 6 | `LowBFret` | `LowB_Finger` | `LowB_Color` | `0x808080` grey |
| 7 | `LowFsFret` | `LowFs_Finger` | `LowFs_Color` | `0x00FFFF` provisional cyan |

The eighth color is **TBD**. It is not an established Rocksmith color. Use
`--eighth-color RRGGBB` to set the compiled fallback, or send `LowFs_Color` per
shape. Color properties that you send override the fallbacks. Send `LowB_Color=0x808080`
for the requested grey seventh string. RGB is a numeric `0xRRGGBB` value, not a
string or an ARGB value. These names identify native indices, whatever the tuning is.

Each shape must follow these rules:

- **6 strings:** omit `LowBFret` and `LowFsFret` (or send actual AS2 `undefined`).
  Do not send `0` or `-1` to mean "absent", because those draw open or muted rows.
- **7 strings:** send `LowBFret`, and omit `LowFsFret`.
- **8 strings:** send both frets. An orphan `LowFsFret` without `LowBFret` is
  ignored and keeps the component at six rows. Muted extra strings still count.
- Fret properties use the **same normalized relative fret window as stock**:
  negative=mute, zero=open, positive=relative fret 1..4. Keep `StartFret` as
  the starting absolute fret of the displayed window. Compute the window over all N
  strings on the native side, because this patch does not normalize absolute frets.
- Finger is optional and numeric, `0..4`. `0` displays `T` (thumb). Omit absent
  fingers instead of sending a negative placeholder value. Open/muted rows have no
  finger labels. Repeated fingers/barres keep the stock per-note text behavior.
- Every update must supply a **complete shape object**. Stock `BaseControl.Update`
  replaces `mDataObj`. If old extra keys stay on a reused native object, the old rows
  stay enabled. Include the legacy fields and handedness/inversion each time.

Legacy fields are unchanged: `LowEFret/LowE_Finger/LowE_Color`,
`AFret/A_Finger/A_Color`, `DFret/D_Finger/D_Color`, `GFret/G_Finger/G_Color`,
`BFret/B_Finger/B_Color`, `HighEFret/HighE_Finger/HighE_Color`, plus
`StartFret`, `Text`, `Visible`, `LeftHanded`, `Inverted`. `LeftHanded=1` mirrors the
horizontal placement, and `Inverted=1` reverses the row order over all N rows.
The optional `ShowHand` and `Finger0Fret..Finger4Fret`/`Finger0Color..Finger4Color`
work as before. Hand fields refer to fingers, not string indices.

This JSON example shows the fields that a native payload adds for an eight-string shape. Merge them into the
complete stock object:

```json
{
  "LowBFret": 1, "LowB_Finger": 1, "LowB_Color": 8421504,
  "LowFsFret": -1, "LowFs_Color": 65535
}
```

Do not extend a native six-element pointer/stack table in place, and do not index the
stock six-color fallback beyond its end. The research documents these risks
and the loaded-template resolver requirement. That research is outside this repository.

## Run it

You need `uv`, Java (tested with Temurin 21), and
[JPEXS FFDec 26.3.0](https://github.com/jindrapetrik/jpexs-decompiler/releases/tag/version26.3.0).
The CLI operations follow the [FFDec CLI documentation](https://github.com/jindrapetrik/jpexs-decompiler/wiki/Commandline-arguments).
Point the tool at an installed `ffdec.jar`. This tool does not bundle or download FFDec.

```powershell
Set-Location tools\gfxpatch
uv run python gfxpatch.py `
  --input "$env:USERPROFILE\Desktop\rocksmith-assets\static.psarc\gfxassets" `
  --output "$env:USERPROFILE\Desktop\rocksmith-assets\edited\gfx" `
  --ffdec '<path to>\ffdec.jar' `
  --java '<path to>\java.exe'
```

The input is the folder that contains `libs/commonlib.gfx`, `views/contentpanel.gfx`,
and `views/ge_game.gfx`. It is **not an archive**. The input and output folders must not
overlap. The tool rejects game/native worktree locations, and output symlinks
or junctions that point outside the output folder. It only reads the original input files.
All three staged copies must pass before the tool writes them to the output. To replace existing edited movies,
pass the `--overwrite` flag. The tool never overwrites the source inputs.
Keep the `libs/` and `views/` folder structure of the output. Textures stay at their original
runtime asset locations. This patcher neither copies nor modifies them.

The command writes the output movies directly to its output path. Next to them,
`gfxpatch-report.json` records hashes, changed tag indices and local evidence
paths. `.work/patch-*/` contains the exported original class, the generated patched
source, the re-decompiled result and the FFDec logs. These intermediate files contain game data, and Git
ignores them.

## Verification and limits

```powershell
uv run python -m unittest -q
node verify_layout.cjs `
  "$env:USERPROFILE\Desktop\rocksmith-assets\edited\gfx\gfxpatch-report.json" `
  '.work\layout-preview.svg'
```

You need Node only for the optional offline drawing harness, not for patching.
`verification.json` contains the final input/output hashes, the changed tag indices,
the test counts and the evidence location from this task's audited run.

The harness runs the **re-decompiled** drawing methods with MovieClip,
TextFormat and Color stubs. **CONFIRMED in that harness:** 606 scenarios pass
across the three classes. They include:

- stock-six snapshot equivalence at init and update
- row transitions 6→7→8→6→8→7→6
- handedness × inversion, 100%/60% scaling
- mute/open/frets 1..4, thumb/repeated/absent fingers, five-finger hand illustration
- default/override colors and the orphan-eighth-field fallback

Binary/path unit tests cover malformed movies, compressed transplant and unsupported sources.

The SVG is an **offline layout schematic that uses our own marker shapes**. It comes
from those executed methods. It is not an actual Flash/Scaleform frame, because no AVM1
player or game rendering was tested. Real bitmap appearance, parent panel
clipping, nine-slice behavior, font rendering, the game's asset loader and native
property delivery remain **HYPOTHESIS / unverified**. The extra height can exceed
an enclosing stock mask, so it must be checked in the chord book, popups and lesson panels
before deployment. The existing lefty behavior for relative frets >4 is out of
scope. The DLL should keep the stock four-fret window policy.

## How edited movies could be loaded — HYPOTHESIS

**Plain PSARC override:** package the user's patched files under the original archive
paths `gfxassets/libs/commonlib.gfx`, `gfxassets/views/contentpanel.gfx` and
`gfxassets/views/ge_game.gfx`, in an unencrypted archive that you created. The existing
plain-PSARC enabler is a possible loading route. But it does **not establish** that
any DLC archive overrides the `static.psarc` UI assets. Mount order, asset
lookup precedence, caches and the time when these movies load still need research and tests.
It is not shown that the edited folder itself works as a loose-file override.

**Runtime route:** a DLL could redirect Scaleform's movie bytes to these edited
copies before each class registration, or inject an AVM1 prototype extension after
loading. This patcher has no hook or injection of its own. RS Extended Range's diagrams feature (`src/features/diagrams/`)
swaps the movies when the game opens them, using the shipped recipes. All
three local class copies matter, because a later movie's registration could replace an
earlier prototype. Keep the CommonLib import linkage and the order of registration.

This task did not prove either route, so neither is deployment advice. This task never accessed
or launched the game folder, modified the native repos, committed or pushed.
