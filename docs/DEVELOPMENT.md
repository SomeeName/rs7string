# rs7string developer guide

**Build, test and tooling notes for rs7string.**

[Contributing](../CONTRIBUTING.md) · **Development** · [Architecture](ARCHITECTURE.md) · [RS7X spec](release/RS7X-SPEC.md) · [Releasing](release/RELEASING.md) · [Agents](../AGENTS.md)

How the mod works is in [ARCHITECTURE.md](ARCHITECTURE.md); player documentation is in
[docs/release](release/README.md); contribution rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Contents

- [Layout](#layout)
- [Build and test](#build-and-test)
- [Formatting](#formatting)
- [Generated files](#generated-files)
- [Evidence policy](#evidence-policy)
- [Debug and test settings](#debug-and-test-settings)
- [Overlay (F9)](#overlay-f9)
- [Site families](#site-families)
- [Stub layer](#stub-layer)
- [Release](#release)

## Layout

| Path | Contents |
|---|---|
| `src/` | The DLL: `core/` (features), `hooks/` (stubs, mid-hooks, site registry), `d3d/` (device hooks), `overlay/` (Dear ImGui UI). |
| `sites.json` | Every patch site: byte pattern, expected stock bytes, kind, note. |
| `tests/` | Native unit tests (`rs7string_tests.exe`, built by `build.ps1`). |
| `tools/` | Site generators and checks (`gen_*.py`, `sitecheck.py`, `check_exports.py`), the pytest suite `tools/tests/` (site table and Unicorn emulator tests), `tools/gfxpatch/` (offline chord-diagram movie patcher), `manifest_key.py`, `psarc_plain.py`, `rsoverlay.py`. |
| `data/headstock/` | Headstock build packages and procedural textures (MIT, see its `LICENSE.md`). |
| `data/chord-diagrams/` | Chord-diagram reconstruction recipes (MIT, see its `LICENSE`). |
| `scripts/` | Release packaging (`package.ps1`, `release.ps1`). |
| `third_party/imgui/` | Dear ImGui, vendored unmodified (MIT). |

## Build and test

Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell, [uv](https://docs.astral.sh/uv/).

```powershell
.\build.ps1                                      # DLL + C++ unit tests (-NoTests: build only)
uv run pytest tools\tests -q                     # site table + emulator tests
uv run python tools\sitecheck.py                 # every site resolves in the game image
uv run python tools\check_exports.py bin\Release\dinput8.dll   # exports, image base, imports
```

What `build.ps1` does:

- Fails first when a source file is not formatted (`scripts\format.ps1 -Check`; `-SkipFormatCheck` skips it).
  Fix with `.\scripts\format.ps1` (clang-format for `src/` and `tests/`, ruff for `tools/` and `scripts/`;
  `third_party/` and `*.generated.h` are never formatted). It needs `clang-format` (on PATH or in Visual Studio).
- Regenerates `src/generated/sites.generated.h` from `sites.json`.
- Passes `/p:PlatformToolset=v145` and `/p:PostBuildEventUseInBuild=false`.
- Copies the headstock packages and the `chord-diagrams` folder next to the DLL.
- Never touches a game folder.
- Output: `bin\Release\dinput8.dll` and `bin\Release\rs7string_tests.exe`.

The unit tests include real-device D3D9 tests (`test_d3d.cpp`, `test_gpu.cpp`: a hidden window, HAL
with REF as fallback). They print `skip: no D3D9 device` only if no device can be created.

> [!IMPORTANT]
> **A game image is needed for part of the gates.** `sitecheck.py` and the emulator tests run the
> stubs against the real x86 code of the game, so they need an unpacked copy of **your own** September
> 2022 executable at `<folder containing the checkout>\original\Rocksmith2014.unpacked.exe` (and
> `Rocksmith2014.analysis.exe` for `sitecheck.py`).
>
> - The executable is VMProtect-packed on disk; the unpacked copy is made from a memory dump of the
>   running game. That pipeline is not part of this repository.
> - The image is Ubisoft's property: never commit or share it.
> - Tests that need it skip when it is missing (`needs original/Rocksmith2014.unpacked.exe`). The
>   native tests and the pure-Python tests (site table, PSARC reader, release scripts) run without it.

**Pytest notes:**

- The tools read `sites.json` and `bin\` of their own checkout, so a git worktree tests itself.
- On Windows, Unicorn's handled first-chance exceptions can flood pytest's faulthandler output. Set
  `PYTEST_ADDOPTS=-p no:faulthandler` (all tests still run).
- Native tests that wait on threads (`test_freeze.cpp` and friends) once had timing assumptions that
  failed under load. They now use bounded waits and fixture cleanup that does not strand thread
  start-up. Keep new thread tests free of fixed sleeps.

## Formatting

`.clang-format`, `[tool.ruff]` in `pyproject.toml` and `.editorconfig` define the style (LF line endings,
120 columns). Run `.\scripts\format.ps1` before you commit; `build.ps1` fails on unformatted sources.
Lint autofix never touches imports (the emulator tests import pytest fixtures that look unused).
Wrap code that clang-format would break in `// clang-format off` / `// clang-format on` with a reason
(for example the include order of `tests/test_d3d.cpp`). The format run is clang-format (which also adds
braces), then `tools\blank_lines.py` (blank lines between statements, rules in [AGENTS.md](../AGENTS.md#code-structure);
it parses with tree-sitter-cpp, a dev dependency installed by `uv run`), then clang-format again.

After a formatting run, check that nothing changed in the code: `.\scripts\compare_builds.ps1 -Old <rev> -New <rev>`
builds both revisions with a linker map and compares every function of `dinput8.dll` and the test exe
(`tools\cmp_functions.py`: calls and data references compare by symbol name, so moved code is equal), and
`tools\cmp_stub_bytes.py` does the quicker exported-stub check (see [Stub layer](#stub-layer)).

## Generated files

`tools/gen_*.py` produce checked-in headers and site entries from the game image (relocated function
copies, native layout patches, tuner session copies, publish ports).

- They are deterministic and checked by tests (`test_fncopy.py`, `test_tuner8_sites.py`, ...).
- Re-run one only after a deliberate change, then run the full gates.
- `tools/audit_eight.py` writes `docs/eight-audit.tsv` (ignored by Git): an exhaustive lexical audit
  of literal 7s and string-6 special cases in the sources.
- `tools/audit_eight_sites.py [GITREF ...]` lists byte patches whose written bytes bound a string loop
  at 7 or 8.

**Offline data pipelines** (need your own extracted game files, not shipped):

| Tool | Produces |
|---|---|
| `tools/gen_headstock.py` | Headstock copy/insert packages from the maintainer's template NIFs and pipeline outputs, into `data/headstock`. Paths default to `%USERPROFILE%\Desktop\rocksmith-assets` (override with `--templates`, `--outputs`, `--dest`). |
| `tools/diagram_patch.py` | Chord-diagram recipes from original and edited movies produced by `tools/gfxpatch` (see its README). Recipes are specific to one movie build; regenerate for another. All three movies must pass the one-tag and byte-identity checks before any recipe is written. |
| `tools/manifest_key.py IN.psarc OUT.psarc [--t6 N]` | Adds the `Rs7String6` key to the arrangement manifests of a plain PSARC. |

## Evidence policy

- Do not commit disassembly or decompiler listings of the game, memory dumps, game logs with personal
  paths, screenshots of game scenes, or anything extracted from game archives.
- Findings belong in the docs as addresses, names and short conclusions, tagged CONFIRMED (with
  evidence) or HYPOTHESIS.
- `.gitignore` excludes `evidence/`, `*-evidence/`, dumps and logs for this reason.

Source comments, `sites.json` notes and tool docstrings that point at `docs/ARCHITECTURE.md` refer to
the topic, not to a numbered section. The longer maintainer research notes and per-job reports they
originally cited were removed from the tree and remain in git history (`26b7aa1`). Two generators and
one test read evidence ledgers from the maintainer's local workspace (`tools/gen_colournative.py`,
`tools/tests/test_fretgrid.py`); the test skips when the ledger is absent.

## Debug and test settings

All keys with defaults are in [SETTINGS.md](release/SETTINGS.md). Developer-relevant ones:

| Key | Use |
|---|---|
| `[debug] diagnostics=1` | Writes the diagnostic sites D2/D3/D5 and lets R2/R1 log D1/D4 (at most 8 lines per log point and string per song). With 0 they are still resolved (a moved pattern still patches nothing) but not written. |
| `[debug] alloc_stats=1` | One `allocstats` line per song setup and song end with process private bytes, working set, free address space (32-bit game), handle count and `kind=live/total` counters for every allocation type the DLL owns (`src/core/allocstats.*`). Over several songs the "song start" lines must be flat in every `live` value; `merges`, `textures`, `bindalt`, `pegCtors` count objects the game keeps for the life of the process and may rise once per distinct headstock model. |
| `[debug] tuner_autotune=N` | Reports each string tuned N seconds after selection (test driving without a guitar). |
| `[debug] skip_sites`, `veh_log`, `spin_probe`, `publish_trace`, `plain_delay_ms`, `diagrams_trace` | See SETTINGS.md; startup-only. |
| `[test] vguitar=1` | Virtual guitarist ([ARCHITECTURE.md section 12](ARCHITECTURE.md#12-virtual-guitarist-test-mode-test-vguitar1)). Keys: `vguitar_mode` (`chart` \| `game` \| `midi:N` \| `off`), `vguitar_filter` (`all` \| strings \| `string:fret`), `vguitar_lead_ms` (40 centres detection), `vguitar_gain`, `vguitar_mix`, `vguitar_trace`, `vguitar_chord6` (`play` \| `omit` \| `only` \| `+N`/`-N` \| `omitlow` \| `omithigh`). All but `vguitar` are re-read at each song start. |

Live versus startup-only: `diagnostics` (D1/D4 only), the colour and neck keys
are live-switchable in the overlay. Keys that decide which hooks exist or size pools at guitar load
(`ramp_swap`, `string7_line`, `headstock_peg`, `background`, ...) are startup-only and
show "restart required".

## Overlay (F9)

A Dear ImGui window with the tabs Status, Colour, Neck, Tuner, Debug, Window and Log. Keys with an
overlay control (the "F9" column in [SETTINGS.md](release/SETTINGS.md)) are marked Live or Restart.
"Save to rs7string.ini" writes only the changed keys.

The overlay renders while the window is unfocused and takes commands from a DirectInput pipe (needs
`[window] background=1`):

```powershell
uv run python tools\rsoverlay.py show | hide | toggle | save | status
uv run python tools\rsoverlay.py tab neck          # status colour neck tuner debug window log
uv run python tools\rsoverlay.py set neck.string7_v 0.5
```

- Each command logs `overlay: cmd ... -> <live | next session | restart required | invalid value |
  unknown key>`.
- Wiring: `src/overlay/`, `src/d3d/present.*`, `src/core/settings.*`, `src/core/overlaycmd.*`.
- The DLL has its own ImGui context (RSMods has another in `xinput1_3.dll`), no `imgui.ini`, no
  XInput, no imm32/shell32 imports (`src/overlay/imconfig_rs7.h`).
- The game device's vtable is rewritten by d3d9 at times (seen at song load). The worker re-checks
  slots 16/17 and 64/65 every second and logs `slots were reset ... hooked again`.

## Site families

`sites.json` is authoritative; each entry's `note` explains it. Id families, for orientation:

| Prefix | Area |
|---|---|
| S1-S4 | Detection: tuning at song setup, expected pitch. |
| R*, BD*, V2, NG* | Lanes, gems, bends, neck targets (the native next-note grid). |
| N1-N23, GS*, GR*, PS*, PG*, PH* | Seventh string line, ghost, bends, native GuitarStrings and PegSet, peg tint. |
| F1, B1/B2 | Fret bars, chord frames. |
| AL1, X_DDSH, X_NF* | Headstock model merge, texture registration. |
| T*, TS*, TS8*, TU8*, L1/L2, PK/PE | Tuner (legacy phases and the native session copies). |
| TJ, TU/TUT, TN | Tuning names. |
| TP*, CP*, NR*, NS*, CT*, MD*, RQ*, AM*, EV*, BT1/CS1, AG*, UP*, PD*, XB1 | Native N-slot chords and RS7X. |
| FGR*, PUB*, NG*, PA*, PM*, PC*, PE0 | Native FretGrid, publication, next-note grid, pitch. |
| GK* | C-lite guitar build (count, equality, trigger, manager, Lua count, headstock loop clamps). |
| DGM, DGS, DGB, TP13 | Chord diagram data. |
| PP_* | Plain PSARC. |
| SE1-SE3 | Song end log. |
| TICK, JUD1, JUD2, AUD1 | Virtual guitarist (written only with `[test] vguitar=1`). |
| `X_*` | References, never written: game globals and callees (`ref`, `callref`). |

Counting rule: all sites resolve or none are written; which sites are written depends on the active
feature switches. `tools/sitecheck.py` reports the totals.

The complete prefix glossary (every family, with counts) and a table of the stubs still in
`src/hooks/stubs.cpp` are in [stubs-refactor-plan.md](stubs-refactor-plan.md).

## Stub layer

`src/hooks/stubs*.cpp` hold the naked stubs, one file per area; each file starts with a map of its stubs.
A stub is exported as `Rs7_Stub_<site id>` with its `g_rs7_resume_<id>` / `g_rs7_target_<id>` variables; these
names never change (the registry, `tools/check_exports.py` and the emulator tests find them by name).
`src/hooks/stub_macros.h` documents the rules every stub keeps and holds the shared macros (declarations, full state
save, entry dispatch of copied functions, guard shapes). New sites should be mid-hooks (`midhook.h`), not new stubs.

A refactor of the stub files must not change the code. Build the previous commit and the new one and run

    uv run python tools\cmp_stub_bytes.py OLD\dinput8.dll NEW\dinput8.dll

It reports `BYTE-IDENTICAL` (all sections equal, debug directory masked) or compares every exported function
instruction by instruction with addresses masked (`SAME INSTRUCTIONS` when stubs only moved).

## Release

See [release/RELEASING.md](release/RELEASING.md) (`scripts/release.ps1`, `scripts/package.ps1`). The
package script refuses game and Ubisoft file types by an allowlist.
