# Native N-string chart format

**The package, manifest, chart and XML specification for RS Extended Range tool authors.**

[Charting](CHARTING.md) · [Chart tools](CHART-TOOLS.md) · **Native format** · [Install](INSTALL.md)

The native format is the only supported on-disk format for extended arrangements. Six-string guitar and
four-string bass arrangements keep their stock format, even inside a native package.

## The three parts

| Part | Stock arrangement or package | Native arrangement or package |
|---|---|---|
| Package | PSARC with `PSAR` magic, `<name>_p.psarc` | Plain PSARC with `PSXR` magic, `<name>_rsxr_p.psarc` |
| Manifest JSON and HSAN entry | Stock attributes and tuning | `ExtendedRange` marker and positional `Tuning` with N entries |
| Inflated SNG body | Stock body | `SNXR` header, then N-wide body and zero padding |

The format is chosen **per arrangement**. Seven- or eight-string guitar and five- or six-string bass arrangements
use native manifests and charts. Six-string guitar and four-string bass arrangements use stock manifest entries
and stock charts. The package uses `PSXR` if at least one arrangement is native. Otherwise it uses `PSAR`.
Vocals, showlights, audio, art and other entries keep their stock formats.

RS Extended Range translates native data when it loads it. Tools write the native data specified here.
There is no trailing extension block and no separate tuning key for an extra string. A game without a compatible
version of RS Extended Range does not list a native package.

## String numbering

String index i always means the i-th string by position, counted from the **thickest**, starting at 0.
The standard open note identifies each string, even when its tuning changes.

| Instrument | Standard open notes in index order (MIDI) |
|---|---|
| guitar 6 | E 40, A 45, D 50, G 55, B 59, high E 64 |
| guitar 7 | B 35, E 40, A 45, D 50, G 55, B 59, high E 64 |
| guitar 8 | F# 30, B 35, E 40, A 45, D 50, G 55, B 59, high E 64 |
| bass 4 | E 28, A 33, D 38, G 43 |
| bass 5 | B 23, E 28, A 33, D 38, G 43 |
| bass 6 | B 23, E 28, A 33, D 38, G 43, C 48 |

Tuning is a whole-number semitone offset per string, from -24 to 24. The open pitch is the standard open MIDI
note plus the tuning offset. A fretted pitch adds the fret to that. Equal pitches, or a string tuned above its
neighbor, do not change the indices. Other string configurations are not supported, such as a five-string bass
with high C but no low B, or a guitar with high A.

## Package

A native package is a **plain (unencrypted) PSARC** with the first four bytes `PSXR` instead of `PSAR`.
Everything else follows the stock archive format. That means the usual entry names, `appid.appid`, and SNG
entries with flags `1` (zlib, unencrypted).

Name a `PSXR` package `<name>_rsxr_p.psarc` and a stock-only `PSAR` package `<name>_p.psarc`.
The magic determines the format. The file name lets players and tools identify the format without opening the file.

## Manifests

Write these fields inside each extended arrangement's `Attributes`, in both its own manifest JSON and its
entry in the HSAN header file:

```json
"ArrangementName": "Bass",
"ExtendedRange": { "Version": 1 },
"Tuning": { "string0": -2, "string1": 0, "string2": 0, "string3": 0, "string4": 0 }
```

- `ExtendedRange` has exactly one member, `"Version": 1`.
- `ArrangementName` equal to `"Bass"` identifies bass. Any other arrangement with a `Tuning` is guitar.
  Do not use `ArrangementProperties.pathBass` to identify HSAN entries, because HSAN entries do not contain it.
- `Tuning` has exactly `string0` through `string{N-1}`, in positional order. N is the number of keys.
  Each value is a whole number from -24 to 24. The instrument must support that count.
- The instrument, string count and tuning must match the chart and both manifest representations.
- Write `ExtendedRange` and `Tuning` literally, without JSON escapes in those names.
- Stock arrangements and arrangements without a `Tuning`, such as vocals, have no marker. All other fields
  keep their stock meaning.

## Tone slots A through Z

A native arrangement can name up to 26 tone-change destinations, `Tone_A` through `Tone_Z`, plus its separate
`Tone_Base`. Put the extra names in the arrangement's `Attributes` next to the stock names:

```json
"Tone_Base": "clean",
"Tone_A": "clean",
"Tone_B": "crunch",
"Tone_C": "drive",
"Tone_D": "lead",
"Tone_E": "ambient",
"Tone_F": "fuzz"
```

Keep complete definitions in `Attributes.Tones` in the full manifest, as for stock tones. Each name must match
exactly one definition's `Key`, compared without case sensitivity. An unused slot may be absent or an empty
string. Names must fit in 255 UTF-8 bytes. Use the same slot names in the manifest JSON and its HSAN entry.
Do not write `RsxrToneSlots`: the loader generates that internal field from valid native attributes.

The SNG tone section retains its stock binary layout: a `u32` event count followed by records containing
`float32 time` and `u32 id`, eight bytes each. Raw IDs `0` through `25` mean A through Z. In particular, ID `5`
means F. Base is selected before the first event; it has no timeline ID. Times must be finite, nonnegative and
chronological. The game selects the last event strictly earlier than the current song time. There is no limit
of 26 changes: only the number of named destinations is limited.

Extra slots require both native manifest attributes and an `SNXR` chart, with at least one nonempty extra
slot E through Z. Stock arrangements and native arrangements without extra slots use stock tone handling.
A missing or ambiguous definition or an out-of-range ID is rejected and leaves the previous sound active.
The stock Tone Plan editor still shows Base and A through D; paging for E through Z is a follow-up.

To prepare a six-tone, 20-change demo from your own native package and six existing definitions, run:

```powershell
python tools/tone_demo.py SOURCE.psarc DEMO_rsxr_p.psarc --arrangement PERSISTENT_ID --keys KEY_A KEY_B KEY_C KEY_D KEY_E KEY_F
```

The first change is at 5 seconds, then every 3 seconds; `--start` and `--interval` change those times.
Choose times within your song and run the generator outside the game folder.

## Charts

The SNG wrapper is the plain-package wrapper. All its numeric values and the inflated body are little-endian.
The wrapper contains, in order:

1. u32 magic `0x4A`.
2. u32 flags `1` (compressed, unencrypted).
3. 16 zero bytes in the unused IV slot.
4. u32 uncompressed size, including the native header, body and padding.
5. The zlib stream containing that complete inflated data.
6. 56 zero bytes in the unused signature slot.

The inflated data starts with:

```text
NativeHeader {
  char magic[4];        // "SNXR"
  u8   version;         // 1
  u8   instrument;      // 0 = guitar, 1 = bass
  u8   stringCount;     // N: guitar 6-8, bass 4-6
  u8   reserved;        // 0
}                       // 8 bytes
Body                    // stock sections in stock order, with the changes below
Padding                 // zero bytes after MetaData
```

Writers use native headers only for extended arrangements. The reader also accepts N = 6 guitar and N = 4 bass
with a native header, but this does not change the rule for writers.

| Record | Stock | Native |
|---|---|---|
| Chord template | `u32 mask; i8 frets[6]; i8 fingers[6]; i32 notes[6]; char name[32]` (72 bytes) | Each of the three arrays has N entries, for 36 + 6N bytes |
| ChordNotes | `u32 mask[6]; BendData32 bend[6]; i8 slideTo[6]; i8 slideUnpitchTo[6]; i16 vibrato[6]` (0x948 bytes) | Every array has N entries, for 396N bytes |
| Note | `stringIndex` 0-5, or -1 for a chord | 0 through N-1, or -1 for a chord |
| Section | `stringMask[36]`, bit s = stock string s | Bit i = native string i |
| MetaData tuning | `i32 count; i16 tuning[count]` | Count = N, in positional order |

The stock definition of `BendData32` and all records not listed above stay unchanged. Beats, phrases, vocals,
phrase iterations, linked difficulties, actions, events, tones, DNAs, anchors, fingerprints and per-phrase counts
are stock. Note, chord and beat masks are technique flags, not string bits.

All chord arrays use the same positional order as notes and tuning. Chord-template `notes` values are kept
for the six standard guitar strings. The mod computes the pitches for the extra guitar strings when it builds chords.

### Padding

The mod translates the chart inside its existing buffer, so the chart needs spare room. Writers must append at
least this many **zero bytes after MetaData**, inside the zlib stream:

```text
N < 6:  256 + (6 - N) * (6 * templates + 396 * chordNotes + 2)
N >= 6: 256
```

`templates` is the number of chord templates and `chordNotes` is the number of ChordNotes records. Include the padding
in the declared uncompressed size. Padding outside the zlib stream does not count.

### Invalid charts

An unsupported or malformed native chart, or one without enough padding, fails to load. Validate the chart before
export, because a failed chart load can leave the player in an empty venue, and the player then has to close the game.

## Arrangement XML

Editors write arrangement XML, and package builders read it. An extended arrangement uses the same positional
numbering throughout the XML and the package:

```xml
<!-- five-string bass, low B tuned down to A -->
<tuning extendedRange="1" strings="5" string0="-2" string1="0" string2="0" string3="0" string4="0" string5="0" />
```

- `extendedRange="1"` on `<tuning>` marks the format and its version. Only extended arrangements carry it.
  Six-string guitar and four-string bass use stock XML without the marker.
- `strings` is required with the marker. It is 7 or 8 for guitar and 5 or 6 for bass. Without the marker, `strings` has
  no meaning for this format.
- Every string index is positional. That covers `string{i}` in `<tuning>`, `fret{i}` and `finger{i}` in `<chordTemplate>`,
  and `string` on `<note>` and `<chordNote>`. Tuning offsets use the standard notes above.
- `<tuning>` always includes at least `string0` through `string5`, with unused values `0`. Seven-string guitar
  adds `string6`, and eight-string guitar adds both `string6` and `string7`. This XML compatibility rule does not
  add unused entries to the manifest or chart tuning.
- The stock XML arrangement name and `pathBass` tell guitar from bass.
- Readers reject an unknown `extendedRange` value. Read a file without the marker as stock. Ignore `strings`
  and the tuning attributes from `string6` up, and reject notes on strings outside the stock instrument's range.

The marker is required because editors already use `strings` with other meanings. An extra tuning attribute on
its own does not identify this format.
