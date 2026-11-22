# RS Extended Range architecture

**How the mod works, which decisions shaped it, and what is known to be open.**

[Contributing](../CONTRIBUTING.md) · [Development](DEVELOPMENT.md) · **Architecture** · [Extension block](architecture/extension-block.md) · [Chart tools](release/CHART-TOOLS.md) · [Releasing](RELEASING.md) · [Agents](../AGENTS.md)

For build, test and tooling, see [DEVELOPMENT.md](DEVELOPMENT.md). Player documentation is in the
[README](../README.md) and [docs/release](release/README.md).

> [!NOTE]
> **Tags.** **CONFIRMED** means checked against the game's code (address given), an emulator run or
> an in-game test. **HYPOTHESIS** means a plausible reading that has not been verified.
> Results described as "automated test runs" or "automated in-game runs" come from the maintainer's private test
> builds, which are not part of the public tree or the released DLL.

> [!IMPORTANT]
> **Addresses** are for the Steam Rocksmith 2014 Remastered executable of September 2022 (PE checksum
> `0x00B13D7C`, image base `0x400000`, 32-bit, VMProtect-packed on disk, unpacked in memory at run
> time). Function names such as `Rnd_FretGrid_GetCellPos` are our own labels from reverse
> engineering, not Ubisoft names. The mod ships no Ubisoft data and uses no Ubisoft keys.

Older notes, per-job reports and review logs from the development of 1.0 were removed from the tree
before the repository went public. They remain only in the maintainer's private development history. Commit
hashes elsewhere in these docs also point into that history, for example those of the frozen stub fixtures in
chapter 14. The fixture files themselves are in the tree.

Native tone registration findings and the isolated E..Z resource-preload design are tracked in
[tone implementation notes](agents/tones-notes.md#native-registration). Full shared-store reader integration
is deferred until its aliases and lifetime are mapped; playback retains the full-object apply path and reads
the stock per-player Tone Plan override properties. The editor's dynamic rows and viewport limit are recorded
in the [demo-first editor notes](agents/tones-notes.md#demo-first-editor-implementation).

## Find a topic

### Start with the model

| Chapter | What it explains |
|---|---|
| [1. What the mod does](architecture/01-what-the-mod-does.md) | From chart data to the built instrument. |
| [2. Patch mechanism](architecture/02-patch-mechanism.md) | Startup checks, sites, hooks and failure handling. |
| [3. String-count model](architecture/03-the-string-count-model.md) | How instrument builds and per-song state agree on the count. |
| [4. Native capacity-eight layouts](architecture/04-native-capacity-8-layouts.md) | The extended arrays used by gameplay features. |

### Gameplay

| Chapter | What it explains |
|---|---|
| [5. Rendering](architecture/05-rendering-the-extra-strings.md) | Lanes, colors, strings, frets and headstocks. |
| [6. Detection and scoring](architecture/06-detection-and-scoring.md) | Expected pitches, low tunings and extended bass. |
| [7. Chords](architecture/07-chords-on-the-extra-strings.md) | Expanded records, matching and rendering. |
| [8. Tuner and tuning names](architecture/08-tuner-and-tuning-names.md) | Tuning order, name resolution and limits. |
| [9. Chord diagrams](architecture/09-chord-diagrams.md) | Extra-string data and in-memory movie patches. |
| [12. Eight strings](architecture/12-eight-strings.md) | The eighth slot, patches and validation. |

### Integration and development

CONFIRMED: the menu load-group update (`0x59DB80`) can wait forever with objectReady (`+0x10`) set and
movieMade (`+0x11`) clear. This is how it happens. The movie request (`0xC192A0`, resolved from its setup call)
asks the asset locator for the menu's Flash file. While boot is still building the local file index, this lookup
fails. The locator then discards the request (`0x906C38 -> 0x906C8D`, state `+0x120` zero) and returns false. The
caller (`0xC19335`) ignores the result, and nothing asks again.

The `menuload` feature handles this case. It records each request at setup (`0xC167A0`) and reads the locator's
answer at `0xC19335` (AL; ESI = movie + 9Ch). The index writer's tail (`0x49DF80`, index lock released,
`[EBP-38h]` = file name) tells it when a file with the requested name is published. On the next load-group update,
the game thread sends that rejected request once more. Accepted requests are never resent. If no matching file is
seen within 5 s, the feature resends the rejected request anyway. It does this every 5 s, at most 6 times, and logs
a warning. This loss path has not been observed yet. Before each request, the feature checks the movie vtable, the owner
and that the Flash instance is absent. Full mid-hooks and a full call-in preserve CPU state without new assembly
stubs.

`[fixes] menu_retry=0` disables the feature. Any mismatch at its sites isolates this feature before writes, and
another mod's detour counts as a mismatch. RSMods' develop branch, for example, hooks `0xC167A0` and `0x59DB80` for
its own timed retry.

| Chapter | What it explains |
|---|---|
| [10. Plain PSARC loading](architecture/10-plain-psarc-loading.md) | Unencrypted custom packages and enabler coexistence. |
| [Native format](NATIVE-FORMAT.md) | The supported extended chart format: N-string packages, manifests and charts, translated at load. |
| [11. Window, input, overlay and logging](architecture/11-window-input-overlay-logging.md) | Window handling, F9 and diagnostics. |
| [13. Known limitations and open risks](architecture/13-known-limitations-and-open-risks.md) | Outstanding checks and release-candidate risks. |
| [14. Mid-hook contract](architecture/14-mid-hook-contract-and-cold-site-conversion.md) | CPU state, callbacks, relocation and conversion tests. |
| [15. Source layout](architecture/15-source-layout.md) | Code layers and how to add a feature. |
