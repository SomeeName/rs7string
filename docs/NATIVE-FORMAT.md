# Native chart format implementation

**Loader translation and development evidence for the native N-string chart format.**

The writer specification lives in [release/NATIVE-FORMAT.md](release/NATIVE-FORMAT.md).
Chart authors should use the [charting guide](release/CHARTING.md). Tool authors should start with
[CHART-TOOLS.md](release/CHART-TOOLS.md). This page records how the mod implements the format and the evidence
that it works.

## Why

The tone-slot loader implementation and static site evidence are recorded in
[tone research notes](agents/tones-notes.md). The writer fields are specified in
[Tone slots A through Z](release/NATIVE-FORMAT.md#tone-slots-a-through-z).

In the native format, the string order is positional throughout the package. The game cannot read this format
directly, so RS Extended Range translates it while loading. The result is stock records plus an internal
[extension block](architecture/extension-block.md) and internal manifest keys. Tools never write that internal form.

## The three parts

The mod checks the package, manifest and chart markers independently. See the public
[three-part overview](release/NATIVE-FORMAT.md#the-three-parts) for the writer rules.

| Part | Translation site |
|---|---|
| Package magic | `PN1` |
| Manifest JSON and HSAN | `JM1` |
| Inflated chart body | `XI1` |

## String numbering

The public [string numbering table](release/NATIVE-FORMAT.md#string-numbering) defines positional indices,
standard open notes and tuning offsets.

The mod maps each native string to the game's internal slot:

| Instrument | native index -> slot |
|---|---|
| guitar 6 | 0-5 -> 0-5 |
| guitar 7 | 0 -> 6, 1-6 -> 0-5 |
| guitar 8 | 0 -> 7, 1 -> 6, 2-7 -> 0-5 |
| bass 4 | 0-3 -> 0-3 |
| bass 5 | 0 -> 4, 1-4 -> 0-3 |
| bass 6 | 0 -> 4, 1-4 -> 0-3, 5 -> 5 |

## Package

The [package specification](release/NATIVE-FORMAT.md#package) defines magic and naming.

The game compares the magic in exactly one place (`0x8A34D0` in the PSARC open function `0x8A3440`, CONFIRMED as the
only `PSAR` compare in the image). If the magic does not match, the game releases the file before it allocates
anything, and it never mounts the package. `PN1` turns `PSXR` into `PSAR` at that compare.

CONFIRMED in the game (2026-11-16). With native packages in `dlc/` and a build without `PN1`, the game starts
normally. The songs are not in the list, and nothing else is affected.

## Manifests

The [manifest specification](release/NATIVE-FORMAT.md#manifests) defines the marker and positional tuning.

`JM1` sits at the entry of the game's JSON parser (`0x889010`). When a text contains `"ExtendedRange"`, the hook
rewrites it before the parser reads it. It removes the marker and turns `Tuning` into the internal form.
That form has `string0`-`string5` in slot order, plus `RsxrString6`/`RsxrString7` for guitar or
`RsxrString4`/`RsxrString5` for bass. See the [internal extension block](architecture/extension-block.md). The parser reads the rewritten copy, and
the game's own buffer stays untouched.

CONFIRMED (source). The internal keys are 11 characters plus a terminator. `nativemanifest.cpp`
appends them to a `std::string`, and `model/manifest.h` passes null-terminated keys to the game
lookup. Neither path uses a fixed key length or rewrites a key in place.

The text handed to the parser is not always zero-terminated right after its closing bracket. This is CONFIRMED in the game
(2026-11-16). With a rewriter that trusted the terminator, three of four header files were left native and the song
list crashed. For this reason the hook takes the text's length from its own closing bracket and reads nothing after it.

Some arrangements are marked but not valid. Each has an unknown version, a string count the instrument does not
have, keys that are not exactly `string0`-`stringN-1`, a value that is not a whole number from -24 to 24, or more
than one `Tuning`. Such an arrangement gets a six-entry `Tuning` built from what is there, so the stock readers
never see a seventh `stringI` key. The song-list reader `0x56D0B0` overruns on `string6`. The log names the arrangement.

`ExtendedRange` and `Tuning` must be written literally. If a text spells them with JSON escapes (`\u0054uning`), the
hook does not recognize them and passes the text to the game as it is. That is a limit of this safety net. It
doesn't open a new hole, because a stock package with a `string6` key reaches the same reader without any marker.

## Charts

The [chart specification](release/NATIVE-FORMAT.md#charts) owns the wrapper, native header and record layouts.

### Conversion in the mod

`XI1` sits in `SNG_Song_Load` `0x558BD0` at `0x558DD6`. It runs after the inflate checks (result 0, inflated length ==
declared size) and before the first section parser. If the body starts with `SNXR`, the hook converts it into the
internal form:

- Chord templates and ChordNotes become stock six-column records. Column `slot` gets native column `i`. A slot
  without a native string gets the stock "unused" values (fret -1, finger -1, note -1, empty technique with slides -1).
- Guitar slots 6 and 7 go into an extension block (`STRS`, `CHDT`, `CHNT`) as the [internal specification](architecture/extension-block.md) defines them.
- Bass keeps all strings in the stock columns. N = 5 or 6 gets an extension block with `STRS` only (instrument 1).
- N = 6 guitar and N = 4 bass get a plain stock body with no block.
- Notes get `stringIndex` = slot, and the section mask bits move to their slots.
- MetaData tuning gets N guitar entries in slot order (0-5, then 6, 7). Bass always gets six entries, with unused = 0.

The game's buffer has a fixed size, which is the declared uncompressed size. The hook therefore copies the result back
**in place**, and the result must not be larger than the native body. With a block, the bytes after MetaData must
tile exactly. For this reason, the rest of the buffer becomes one more extension block section with an unknown id
and no critical bit (`PAD0`), which readers skip.

### Padding

The writer's [padding formula](release/NATIVE-FORMAT.md#padding) reserves space for in-place conversion.
The converter checks the actual sizes and, if space is missing, logs how many bytes are missing.

### A chart that cannot be converted

The load fails. The hook continues at the loader's own failure exit `0x559249` (CONFIRMED as the target of the stock
`jne` branches at `0x558DBF` / `0x558DCD`, in the same stack frame). That exit destroys both streams and returns false. The stock
parser never runs over an unconverted body.

CONFIRMED in the game (2026-11-16). After such a failed load, the game does not crash and stays responsive. But it
sits in the empty venue without a note highway, and the pause menu does not open, so the player has to close the game.
So the hook is only a last line of defense, and the player gets no error message. Tools must write valid charts.

This behavior is the game's own handling of a chart that does not load. The load job `0x568AE0` drops the song
object and sets the request's failure byte (+1E0h), and nothing in the song screen reacts to it (CONFIRMED in the
decompiler for the job). That a damaged stock chart ends in the same empty venue is a HYPOTHESIS and was not tried.

## Arrangement XML

The mod never reads arrangement XML. Editors and package builders follow the
[version 1 XML rules](release/NATIVE-FORMAT.md#arrangement-xml). The explicit marker tells those rules apart
from other ways editors use the strings attribute.

## Converting existing charts

`tools/native_chart.py to-native IN.psarc OUT.psarc` converts former extended packages to native packages. It
converts the charts, manifests, header files and the package magic. The mod itself does not recognize the former
extended format.

- Conversion checks that every instrument chart, manifest and header-file entry agrees on the instrument and
  string count. It rejects unsupported extension sections, name-byte chord extensions, non-integral offsets,
  offsets outside -24..24 and conflicting bass tuning values.
- Six-string guitar and four-string bass arrangements keep their exact bytes. A package containing only stock
  arrangements stays a `PSAR` package.
- Manifest JSON is written with two-space indentation. Line endings, byte-order mark and final newline are kept.
- `to-stock` remains an offline round-trip utility that produces the former format. Its output is not a supported
  extended package for the mod and does not use the renamed internal representation. No mod test needs it.
  The converter and its existing tests keep their former-format identifiers and behavior.

## Verified in the game

CONFIRMED (2026-11-16, one build, automated playthrough). The 49 charts of the test suite, converted to native packages
with `tools/native_chart.py`, were compared with the same 49 in the former format. That run used the first layout of
the format. It had a chart header of 12 + N bytes with an open-note list and earlier identifiers, and every
arrangement was native.

| Instrument | Strings | Charts | Chart data seen by the game |
|---|---|---|---|
| bass | 4 | 2 | identical |
| bass | 5 | 11 | identical |
| bass | 6 | 11 | identical |
| guitar | 6 | 4 | identical |
| guitar | 7 | 12 | identical |
| guitar | 8 | 12 | identical |

The count includes both parts of the three guitar + bass packages, which gives 52 arrangements from 49 packages. From the
packages with 5, 8 and 10 arrangements, one arrangement each was played. The others were only checked offline.
For each chart, the run compared every note the game hands to the note detection (time, string, fret, chord,
pitch, technique mask), the extension block read by that historical build, the tuning names and the string count.
All 49 header files were rewritten, no arrangement was reported invalid, no load was refused and no hook fault
was logged.

Hits differ on single notes in 11 of the 49 charts, in both directions. A second run of eight of those
charts in the former format differs from the first in the same way. So the difference is the normal spread between
two automated runs and doesn't come from the format.

CONFIRMED (2026-11-16 evening, build of commit 31af1f85) for the layout described in this document, with the eight-byte
header, `PSXR` / `SNXR` / `ExtendedRange` and native per arrangement. A sample of ten plays was compared with the
former format. The sample covered:

- bass 5 and 6, guitar 7 and 8
- a six-string control (a `PSAR` package, untouched)
- the bass parts of two guitar + bass packages
- the three packages that mix stock and native arrangements, one arrangement each, among them a stock
  four-string bass arrangement inside a `PSXR` package

The notes handed to the note detection are identical in all ten, no load was refused and no hook fault was logged.
The other 37 packages of the suite and the stock six-string guitar arrangements inside the mixed packages were not
played in this layout. The 37 packages were converted and converted back offline, and the results were equal.

Both runs lost one song load after about 45 songs in one game session. It was the 45th in the native run and the 46th
in the former-format run. The game enters the venue and never asks for the chart. This is not tied to the format, because
the charts in question play in a fresh session. It was not investigated here.

## Not done

- More than eight guitar strings or six bass strings. This needs work on the game side first. The format doesn't need to change.
- Growing the game's body buffer instead of relying on writer padding.
- Native-format export in other chart tools. The public guide uses DotCharter's RS Extended Range export.

## Pieces

| Piece | Where |
|---|---|
| Chart converter (pure, unit-tested) | `src/model/nativesng.h`, `src/model/nativesng.cpp`, `tests/model/test_nativesng.cpp` |
| Manifest rewriter (pure, unit-tested) | `src/model/nativemanifest.h`, `src/model/nativemanifest.cpp`, `tests/model/test_nativemanifest.cpp` |
| Hooks `PN1`, `JM1`, `XI1` | `src/features/native/`, `sites.json`, `tests/native/test_hooks.cpp` |
| Reference converter (package in, package out, both ways) | `tools/native_chart.py`, `tools/psarc_plain.py`, `tools/tests/test_native_chart.py` |
| The converter players get (`tools/rsxr-convert.exe` in the release zip, built by `scripts/package.ps1`) | `tools/rsxr_convert.py`, `tools/tests/test_rsxr_convert.py` |
