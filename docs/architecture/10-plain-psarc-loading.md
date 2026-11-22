# 10. Plain (unencrypted) PSARC loading

Part of the [RS Extended Range architecture](../ARCHITECTURE.md).

`[psarc] plain=1` (the default) lets the game read unencrypted custom packages. The code is in
`src/features/psarc/`. Its three sites form the `psarc` group, which the mod resolves and writes first,
on its own. A failure there disables only plain loading. You still need a CDLC enabler for the SNG
signature bypass (`0x5582F0`), which RS Extended Range does not touch.

## Package paths

The mod recognizes a plain TOC and a plain SNG. Everything else takes the game's own path. **CONFIRMED** by emulator and in-game checks:

| Site | Purpose |
|---|---|
| PP_TOC `0x8A3643` | At the TOC block (`0x8A3640..0x8A368F`), archive flag mask `0x4` clear means a plain TOC. |
| PP_SNGF `0x558C1A` | Allows flags 1 through the `cmp [esp+68h],3` check. |
| PP_SNGD `0x558CD4` | For magic `0x4A` and flags 1, copies the payload after the 16-byte IV slot; otherwise calls the game's original routine `0xE3EA10`. |

## Coexistence with an enabler

If another mod has already written any of the three sites, RS Extended Range leaves all three alone and
logs `plain psarc: provided by the CDLC enabler`. An enabler that loses this race can print
"Unencrypted CDLC will not load" even though plain packages load. That message is cosmetic.

## Checks and open risk

- The 2026-10-14 test matrix: plain and encrypted packages and official DLC all played. With `plain=0`
  and an enabler without plain support, the game aborted after profile selection.
- **Open risk:** a locked 8-byte exchange is atomic for data, but cross-modifying code needs
  serialization on the executing core. The upstream enabler's patch makes the same assumption. No SNG
  is loaded at patch time.
