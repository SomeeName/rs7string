# rs7string architecture

**How the mod works, which decisions shaped it, and what is known to be open.**

[Contributing](../CONTRIBUTING.md) · [Development](DEVELOPMENT.md) · **Architecture** · [RS7X spec](release/RS7X-SPEC.md) · [Chart tools](release/CHART-TOOLS.md) · [Releasing](RELEASING.md) · [Agents](../AGENTS.md)

For build, test and tooling see [DEVELOPMENT.md](DEVELOPMENT.md); for players see the
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
hashes elsewhere in these docs (for example the frozen stub fixtures in chapter 14) name that history too; the
fixture files themselves are in the tree.

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
| [5. Rendering](architecture/05-rendering-the-extra-strings.md) | Lanes, colours, strings, frets and headstocks. |
| [6. Detection and scoring](architecture/06-detection-and-scoring.md) | Expected pitches, low tunings and extended bass. |
| [7. Chords](architecture/07-chords-on-the-extra-strings.md) | Expanded records, matching and rendering. |
| [8. Tuner and tuning names](architecture/08-tuner-and-tuning-names.md) | Tuning order, name resolution and limits. |
| [9. Chord diagrams](architecture/09-chord-diagrams.md) | Extra-string data and in-memory movie patches. |
| [12. Eight strings](architecture/12-eight-strings.md) | The eighth slot, patches and validation. |

### Integration and development

CONFIRMED: the menu load-group update (`0x59DB80`) can wait indefinitely with objectReady (`+0x10`) set and
movieMade (`+0x11`) clear. The movie request (`0xC192A0`, resolved from its setup call) asks the asset locator for
the menu's Flash file; while boot is still building the local file index, the lookup fails, the locator discards
the request (`0x906C38 -> 0x906C8D`, state `+0x120` zero) and returns false, and the caller (`0xC19335`) ignores
the result. Nothing asks again. The `menuload` feature records each request at setup (`0xC167A0`), reads the
locator's answer at `0xC19335` (AL; ESI = movie + 9Ch), and is told by the index writer's tail (`0x49DF80`, index
lock released, `[EBP-38h]` = file name) when a file with the requested name is published. On the next load-group
update the game thread sends that rejected request once more. Accepted requests are never resent. Only if no
matching file is seen within 5 s (a loss path not observed yet) is a rejected request resent anyway, every 5 s and at
most 6 times, with a warning in the log. It checks the movie vtable, owner and absent Flash instance before each
request. Full mid-hooks and a full call-in preserve CPU state without new assembly stubs. `[fixes] menu_retry=0` disables it;
any mismatch at its sites isolates this feature before writes, including another mod's detour (RSMods' develop
branch hooks `0xC167A0` and `0x59DB80` for its own timed retry).

| Chapter | What it explains |
|---|---|
| [10. Plain PSARC loading](architecture/10-plain-psarc-loading.md) | Unencrypted custom packages and enabler coexistence. |
| [11. Window, input, overlay and logging](architecture/11-window-input-overlay-logging.md) | Window handling, F9 and diagnostics. |
| [13. Known limitations and open risks](architecture/13-known-limitations-and-open-risks.md) | Outstanding checks and release-candidate risks. |
| [14. Mid-hook contract](architecture/14-mid-hook-contract-and-cold-site-conversion.md) | CPU state, callbacks, relocation and conversion tests. |
| [15. Source layout](architecture/15-source-layout.md) | Code layers and how to add a feature. |
