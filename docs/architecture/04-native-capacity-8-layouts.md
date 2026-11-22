# 4. Native capacity-8 layouts

Part of the [RS Extended Range architecture](../ARCHITECTURE.md).

The native layouts give game systems room for up to eight strings. The layout features form one
unit (`Rsxr_IsSevenUnit`) with no ini switch ([chapter 2](02-patch-mechanism.md#coexistence-and-safety)).
Chord loops use these layouts, but they are not a member of the unit.

## At a glance

| Area | Capacity-8 change |
|---|---|
| Strings and pegs | Extra main, ghost and peg slots let stock-style loops reach strings 6 and 7. |
| Colors | New eight-slot tables next to the original six-slot tables, which RSMods uses. |
| FretGrid and next-note grid | Each grid has room for eight string positions. |
| Pitch | Detector and matcher tails store eight tunings, open pitches and the active count. |
| Tuner and chords | The tuner and N-slot chord records use the extended layouts. |

## Layout and site reference

The original six-slot data stays in place wherever other code (stock readers, RSMods) still reads it.

<details markdown="1">
<summary><b>Native layouts: offsets and hooks</b></summary>

| Area | Runtime switch | What changes |
|---|---|---|
| GuitarStrings | `g_rsxr_native_strings` | Capacity-8 arrays (`+40h` main, `+60h` ghost; the stock `+4`/`+1Ch` six stay as aliases). InstrumentInfo grows to `0x48` with its own counts (`+40h` active, `+41h` loaded; `+0Bh` stays the stock count). Bind, load, unload, fade, transforms, update, reset, pluck, vibration, InitColors, free, section dimming/glow choice `0x460550`, glow ramp `0x5441A0` and headstock segment colors `0x58E4F0` run as relocated copies (GS* sites). Song start/end hooks GSSS/GSSE. (`src/features/strings/strings.h`) |
| PegSet | `g_rsxr_native_pegset` | 8 slots; pegs 7 and 8 are slots 6 and 7, so stock fade/layer/tint loops cover them. |
| Color manager | `g_rsxr_native_colours` | `*0x135F58C`: original six-slot tables kept for RSMods; eight-slot tables appended (both color modes). |
| FretGrid | `g_rsxr_native_fretgrid` | Cell rows of `264h` instead of `1CCh`; hooks FGR*, PUBGC/GO/GB/GA/GF/EDGE/GP. `Rnd_FretGrid_GetCellPos` `0x58FE00` is answered from native cells for strings 0-7. |
| Pitch | `g_rsxr_native_pitch` | Detector grows to `13E0h` (int16 tuning8 `+13B8h`, openMidi8 `+13C8h`, active count `+13D8h`), matcher to `6Ch`. `0x873E90` is replaced by a port (PE0); the matcher's calls of `0x873F80` (PC1-PC4) read strings >= 6 of a tailed object from the tail (`rsxrp::ExpectedPitch8`). Guitar strings 0-7 read an eight-entry base table (35 / 30 for strings 6 / 7); bass strings 4 / 5 of an extended bass use 23 / 48. |
| Pitch tails | | Tails are written only into blocks that `_msize` reports as large enough. The writers record each tailed object in a registry (`rsxrp::kTailedMax` = 128 entries). When it is full, the least recently written entry is evicted and logged once. A writer that finds a stock-size block drops a stale entry. `RsxrPitch_Tailed` reads the registry, so the hot readers call no CRT function and can run in Light mode. CONFIRMED peak in game: 2 objects, 0 evictions. |
| Next-note grid | `g_rsxr_native_nextgrid` | 8 time rows (sites NGB*). |
| Tuner | `g_rsxr_native_tuner` | See [chapter 8](08-tuner-and-tuning-names.md). |
| Chords | CL* sites (need the native strings; not a unit member), N-slot sites | See [chapter 7](07-chords-on-the-extra-strings.md). |

</details>

CONFIRMED: InstrumentInfo construction sets six strings at `0x58AB76`. FromProfile later writes the profile count
at `0x58ABF3`. GIP1 runs after that call (`0x4B240B`) and before per-string initialization (`0x4B2416`). It caps the
count at six when extra-string construction failed or the native InstrumentInfo registry was full. Native counts
are refreshed only within registered allocations. Partially constructed extras still use the native free path.

## Stock readers not ported

These stock fixed-6 readers were judged cosmetic or guarded, so they are not ported (HYPOTHESIS: none
indexes past its array):

- renderer glow `0x54424B` / glow color `0x544366` for strings 6/7
- BD1-BD4 bend notation for string 7
- mode readers `0x59A920` / `0x7A6390`
- chord residual loops `0x7E397A`, `0x7E3C57`, `0x7E42A1`, `0x7A6280`
