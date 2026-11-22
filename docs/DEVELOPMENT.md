# RS Extended Range developer guide

**Build, test and tooling notes for RS Extended Range.**

[Contributing](../CONTRIBUTING.md) · **Development** · [Architecture](ARCHITECTURE.md) · [Extension block](architecture/extension-block.md) · [Chart tools](release/CHART-TOOLS.md) · [Releasing](RELEASING.md) · [Agents](../AGENTS.md)

This guide covers building, testing and the tools. [ARCHITECTURE.md](ARCHITECTURE.md) explains how the mod works.
The player documentation is in [docs/release](release/README.md), and the contribution rules are in
[CONTRIBUTING.md](../CONTRIBUTING.md).

<details class="inline-contents" markdown="1">
<summary><b>Contents</b></summary>

- [Layout](#layout)
- [Build and test](#build-and-test)
- [Continuous integration](#continuous-integration)
- [Fuzzing](#fuzzing)
- [Formatting](#formatting)
- [Generated files](#generated-files)
- [Evidence policy](#evidence-policy)
- [Developer and test settings](#developer-and-test-settings)
- [Overlay (F9)](#overlay-f9)
- [Site families](#site-families)
- [Stub layer](#stub-layer)
- [Release](#release)

</details>

## Layout

The path table is in [Code structure and repository layout](agents/code-structure.md#layout).

## Build and test

You need Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell and [uv](https://docs.astral.sh/uv/).

```powershell
.\build.ps1                                      # DLL + C++ unit tests (-NoTests: build only)
uv run pytest tools\tests -q                     # site table + emulator tests
uv run python tools\sitecheck.py                 # every site resolves in the game image
uv run python tools\check_exports.py bin\Release\dinput8.dll   # exports, image base, imports
```

CI ([Continuous integration](#continuous-integration)) runs only the gates that need no game files. Run the rest
locally before every pull request, with the commands above.

`build.ps1` checks formatting and layout, regenerates the site table, builds and tests the DLL, and runs static
analysis. It never copies into a game folder. Open the details below for the flags and each gate.

<details markdown="1">
<summary><b>Build steps and options</b></summary>

What `build.ps1` does:

- It fails first if a source file is not formatted (`scripts\format.ps1 -Check`). `-SkipFormatCheck` skips this check.
  To fix it, run `.\scripts\format.ps1`. It uses clang-format for `src/` and `tests/` and ruff for `tools/` and
  `scripts/`. It never formats `third_party/` and `*.generated.h`. It needs `clang-format`, on PATH or in Visual Studio.
  The same step runs the layout check (`tools\check_layout.py`, see [Code structure](agents/code-structure.md)),
  and `-SkipFormatCheck` skips it too.
- Regenerates `src/generated/sites.generated.h` from `sites.json`.
- Passes `/p:PlatformToolset=v145` and `/p:PostBuildEventUseInBuild=false`.
- Compiles both projects at `/W4` with warnings as errors (`/WX`, `TreatWarningAsError` in both `.vcxproj`
  files), so any new compiler warning fails the build. `third_party/` (Dear ImGui) compiles with warnings off.
  Fix a warning instead of disabling it. If a fix would change behavior (an intentional `alignas`
  padding, for example), disable only that warning code with `#pragma warning(push/disable/pop)` around the
  code, and add a comment that says why.
- Copies the headstock packages and the `chord-diagrams` folder next to the DLL.
- Runs MSVC static analysis last. Both projects compile again with `/analyze` (`analyze.props`, switched on by
  `/p:RsxrAnalyze=true`, in its own `obj\analyze` folder; `src/` and `tests/`, not `third_party/`). Then
  `tools\check_analyze.py` compares the findings (keyed `file::function::Cnnnn`) with
  `tools\analyze_baseline.json`. Like the layout baseline, this baseline may only shrink.
  - Rules: the Build Tools' default native set (null derefs, buffer overruns, uninitialized reads, API contracts,
    leaks), minus C6320 / C6322 (the deliberate catch-all `__except` guards) and C28159 (`GetTickCount64` advice).
  - Time: about 2 minutes from clean, seconds incrementally. `-SkipAnalysis` skips it for a quick local loop, but
    not for a pull request.
  - Fix a new finding. Only a false positive goes into the baseline. Run `--write-baseline --allow-grow`, then fill
    in its `note` with the reason it is a false positive. Plain `--write-baseline` only drops fixed entries.
- With `-Asan` (optional, off by default), it builds the unit tests again with AddressSanitizer (`asan.props`,
  `/p:RsxrAsan=true`, own `obj\asan` and `bin\Asan` folders). The DLL is never built with ASan. The step copies the
  x86 ASan runtime from the MSVC toolset next to the exe and runs it. An ASan report (heap/stack overflow, use after
  free) or a failing test fails the build. This takes about a minute. A test whose inline asm uses EBX (ASan's frame
  pointer, C5073) is declared with `TEST_NO_ASAN` instead of `TEST`. Run this step before a pull request that touches
  memory handling.
  With `-NoTests` the normal test run is skipped, but the ASan tests still run.
- A unit test that depends on how Windows schedules threads (deadlines of a few milliseconds, the startup freeze
  tests) is declared with `TIMING_TEST`. It gets up to three attempts, and each failed attempt is printed as
  `RETRY`. Use it only for scheduling, never to hide a logic failure.
- `-Toolset <vNNN>` builds with another platform toolset. `-Toolset auto` takes `v145` if it is installed, and
  otherwise the newest one (CI). Without the VS 2026 Build Tools path, MSBuild comes from `vswhere`.
- With `-Fuzz` (optional), every libFuzzer target of the parsers runs for 30 s afterwards (`scripts\fuzz.ps1`, own
  project and `bin\Fuzz`; see [Fuzzing](#fuzzing)).
- With `-Public`, it makes the build that ships (`scripts\package.ps1` uses it). This build leaves out the private extras
  (`src/features/extras/extras.h`: `/p:RsxrPrivate=false`, `private.props`, `RSXR_PRIVATE=0`, their sites via `gen_sites.py --without`).
  A tree without them (the public repository) builds that way by default.
- It never touches a game folder.
- Output: `bin\Release\dinput8.dll` and `bin\Release\rsxr_tests.exe`.

</details>

The unit tests include real-device D3D9 tests (`test_d3d_*.cpp`, `test_gpu.cpp`: a hidden window, HAL
with REF as fallback). They print `skip: no D3D9 device` only if no device can be created.

> [!IMPORTANT]
> **Some gates need a game image.** `sitecheck.py` and the emulator tests run the
> stubs against the real x86 code of the game. So they need an unpacked copy of **your own** September
> 2022 executable at `original\Rocksmith2014.unpacked.exe` (and `Rocksmith2014.analysis.exe` for
> `sitecheck.py`). The tools look in the nearest folder above the checkout that has an `original\` folder, or in
> the folder that `RSXR_GAME_FILES` names (`tools/game_files.py`).
>
> - The executable is VMProtect-packed on disk. The unpacked copy is made from a memory dump of the
>   running game. That pipeline is not part of this repository.
> - The image is Ubisoft's property: never commit or share it.
> - Tests that need it skip when it is missing (`needs original/Rocksmith2014.unpacked.exe`). The
>   native tests and the pure-Python tests (site table, PSARC reader, release scripts) run without it.

**Pytest notes:**

- The tools read `sites.json` and `bin\` of their own checkout, so a git worktree tests itself.
- On Windows, Unicorn's handled first-chance exceptions can flood pytest's faulthandler output. Set
  `PYTEST_ADDOPTS=-p no:faulthandler` (all tests still run).
- Native tests that wait on threads (`test_freeze.cpp` and others) use bounded waits and fixture cleanup that
  does not strand thread start-up. Keep new thread tests free of fixed sleeps.
- `.\scripts\pytest_nogame.ps1` runs pytest as CI does, with no game files, to check that every test that needs
  them skips cleanly.

## Continuous integration

`.github/workflows/ci.yml` runs every gate that needs no game files, on pull requests and pushes to `main`
(windows-latest):

- `build.ps1 -Toolset auto -Asan -SkipAnalysis` (format and layout check, the Release Win32 build, the
  native unit tests, the unit tests under ASan)
- `tools\check_exports.py`
- `pytest tools\tests` with `RSXR_GAME_FILES` pointing at an empty folder

`tools/game_files.py` looks in that folder for the user's own files. These are the unpacked and analysis exe in
`original/`, the memory snapshot in `rskeys/snapshot`, and the research ledgers. By default, the folder is the one that contains
the checkout. A test that needs one of these files skips without it (`emu_common.needs_exe`, the emulator's
`needs_emu`). If a module reads the exe or snapshot while pytest imports it, the loaders skip the whole module.
To run the same check locally, use `.\scripts\pytest_nogame.ps1` (it sets the variable for that call only). The
pytest header prints the game-files root and whether the exe and the snapshot were found.

The static analysis gate runs in a separate job that is allowed to fail. Its baseline was recorded with MSVC 14.51,
and the hosted images ship another MSVC. Make it required once they match. `sitecheck.py` and the emulator tests run only
locally. clang-format is pinned to the version the sources are formatted with.

## Fuzzing

The parsers of untrusted input have libFuzzer targets (`fuzz\`, one exe, `bin\Fuzz\rsxr_fuzz.exe`), built with
the MSVC Build Tools' own libFuzzer and AddressSanitizer for x86 (`/fsanitize=address /fsanitize=fuzzer`). The fuzz
project is separate from the default build and its tests. The manifest JSON readers in `model/manifest.h`
aren't fuzzed. They walk the game's own JSON objects through their vtables, so there is no byte input without the game.

| Target | Input |
|---|---|
| `extblock` | extension block (`model/extblock`), with the song's template / ChordNotes / tuning counts |
| `settings` | `RsxrSet_SetText` for every key, then `RsxrSet_Format` |
| `ini` | the bytes as `rsxr.ini`: every feature's startup read and `RsxrSet_InitFromIni` (a seed covers the over-long value rule: more than 259 characters is rejected) |
| `text` | pipe commands, hotkeys, DirectInput key lines, colors, string spacing, the private extras' parsers |
| `package` | headstock build package manifest (`rsxrhs::Manifest`) |
| `nif` | texture names in a NIF (`RsxrAsset_DdsNames`) |
| `dds` | DXT1 darkening of a `.dds` (`RsxrAsset_ScaleDxt1`) |
| `recipe` | chord-diagram recipe against a movie (`rsxrdiagram::SwapMovie`; the fuzz build skips its SHA-256 checks) |
| `nslot` | stock chord template + extension block into the N-slot T8 / C8 records, eval tails and the chord diagram's shape (`features/nslot/records`, `rsxrdiagram::FromT8` / `Emit`) |
| `plain` | plain SNG copy (`Rsxr_Stub_PP_SNGD` for SNG flags 1; other flags are the game's decrypt and are skipped) |
| `nativesng` | native chart body into the internal form (`rsxrnative::Convert`, [NATIVE-FORMAT.md](NATIVE-FORMAT.md)) |
| `nativemanifest` | native manifest text: its extent and the rewrite (`rsxrnative::JsonExtent`, `RewriteManifest`) |

```powershell
.\build.ps1 -Fuzz                                    # the normal build, then every target 30 s
.\scripts\fuzz.ps1 -Seconds 3600 -Target extblock,recipe # longer runs; -NoBuild reuses the exe
```

The corpus grows in `obj\fuzz\corpus\<target>` across runs. The seeds (built in, plus the shipped headstock packages and
recipes) are in `obj\fuzz\seeds\<target>`. Findings land in `obj\fuzz\artifacts\<target>\`. To replay one, run
`$env:RSXR_FUZZ_TARGET = '<target>'; bin\Fuzz\rsxr_fuzz.exe -artifact_prefix=obj\fuzz\artifacts\<target>\ <file>`.
Without the prefix, libFuzzer writes into the current folder (`.gitignore` covers `crash-*` and similar files). A fixed
seed (`-seed=1`) keeps the short run reproducible. Each fixed finding gets a native regression test with the input in `tests/`.

## Formatting

`.clang-format`, `[tool.ruff]` in `pyproject.toml` and `.editorconfig` define the style (LF line endings,
120 columns). Run `.\scripts\format.ps1` before you commit, because `build.ps1` fails on unformatted sources.
Lint autofix never touches imports, because the emulator tests import pytest fixtures that look unused.
If clang-format would break some code, wrap it in `// clang-format off` / `// clang-format on` and give a reason
(for example the include order of `tests/test_d3d_hook.cpp`). The format run has three passes:

1. clang-format, which also adds braces.
2. `tools\blank_lines.py`, which sets the blank lines between statements (rules in [code-structure.md](agents/code-structure.md#code-structure)).
   It parses with tree-sitter-cpp, a dev dependency that `uv run` installs.
3. clang-format again.

`build.ps1` then runs `tools\check_layout.py`. It checks file and function size limits, inline assembly and the include
direction of the source layers ([code-structure.md](agents/code-structure.md#code-structure)). Known violations are listed in
`tools/layout_baseline.json`. After you fix one, run `uv run python tools\check_layout.py --write-baseline`.

After a formatting run, check that nothing changed in the code. `.\scripts\compare_builds.ps1 -Old <rev> -New <rev>`
builds both revisions with a linker map and compares every function of `dinput8.dll` and the test exe
(`tools\cmp_functions.py`). Calls and data references compare by symbol name, so moved code counts as equal.
`tools\cmp_stub_bytes.py` does the quicker exported-stub check (see [Stub layer](#stub-layer)).

When files are moved or split, add `-NoOpt`. Both revisions are then built with `/Od /Ob0` (no optimization, no
inlining; cached in `..\rsxr-cmp-out\<sha>-noopt`). This is needed because in the Release build a split changes inlining and
frame layout. Symbol names that move with the source path (anonymous namespace, string literals) are
normalized, so a pure move compares equal and a changed function body is still reported. `-Alias <namespace>`
counts a named namespace as the anonymous one, for code moved out of `namespace { }`. The result proves the
code is unchanged, not that the optimized build is.

## Generated files

`tools/gen_*.py` produce checked-in headers and site entries from the game image (relocated function
copies, native layout patches, tuner session copies, publish ports).

- They are deterministic, and tests check them (`test_fncopy.py`, `test_tuner8_sites.py`, ...).
- Never edit generated headers (`src/generated/`, `*.generated.h`) by hand. Change the `tools/gen_*.py` input and regenerate.
- Re-run a generator only after a deliberate change, then run the full gates.
- `tools/audit_eight.py` writes `docs/eight-audit.tsv` (ignored by Git). This is a full text search
  for literal 7s and string-6 special cases in the sources.
- `tools/audit_eight_sites.py [GITREF ...]` lists byte patches whose written bytes bound a string loop
  at 7 or 8.

**Offline data pipelines.** These need your own extracted game files and are not shipped:

| Tool | Produces |
|---|---|
| `tools/gen_headstock.py` | Headstock copy/insert packages from the maintainer's template NIFs and pipeline outputs, into `data/headstock`. Paths default to `%USERPROFILE%\Desktop\rocksmith-assets` (override with `--templates`, `--outputs`, `--dest`). |
| `tools/diagram_patch.py` | Chord-diagram recipes from original and edited movies produced by `tools/gfxpatch` (see its README). Recipes are specific to one movie build. Regenerate them for another build. All three movies must pass the one-tag and byte-identity checks before any recipe is written. |

## Evidence policy

- Do not commit disassembly or decompiler listings of the game, memory dumps, game logs with personal
  paths, screenshots of game scenes, or anything extracted from game archives.
- Findings belong in the docs as addresses, names and short conclusions, tagged CONFIRMED (with
  evidence) or HYPOTHESIS.
- `.gitignore` excludes `evidence/`, `*-evidence/`, dumps and logs for this reason.

Source comments, `sites.json` notes and tool docstrings that point at `docs/ARCHITECTURE.md` refer to
the topic, not to a numbered section. The maintainer's research notes are not in the repository. One generator
(`tools/gen_colournative.py`) and one test (`tools/tests/test_fretgrid.py`) read evidence ledgers from the
maintainer's local workspace. The test skips if the ledger is missing.

## Developer and test settings

The player keys are in [SETTINGS.md](release/SETTINGS.md). The keys below still work, but the release template
(`scripts/package.ps1`, `$devOnly`) leaves them out, and the overlay has no control for them. Set them in the file or
with `o set <section.key> <value>` over the pipe (`tools/rsoverlay.py`).

| Key | Default | Use |
|---|---|---|
| `[debug] diagnostics=1` | `0` | Writes the diagnostic sites D2/D3/D5 and lets R2/R1 log D1/D4 (at most 8 lines per log point and string per song). With 0 they are still resolved (a moved pattern still patches nothing) but not written. A player key too. |
| `[debug] alloc_stats=1` | `0` | One `allocstats` line per song setup and song end with process private bytes, working set, free address space (32-bit game), handle count and `kind=live/total` counters for every allocation type the DLL owns (`src/features/allocstats/allocstats.*`). Over several songs the "song start" lines must be flat in every `live` value; `merges`, `textures`, `bindalt`, `pegCtors` count objects the game keeps for the life of the process and may rise once per distinct headstock model. Startup-only. |
| `[debug] tuner_autotune=N` | `0` | Reports each string tuned N seconds (1-30) after selection (test driving without a guitar). Live. |
| `[debug] head_cull=<node>` | empty | Keeps the named headstock frame node hidden on 7/8-string guitars. Live. |
| `[debug] veh_log=1` | `0` | Logs the first 40 exceptions, plus a separate budget of 40 in the game, through a vectored exception handler. Startup-only. |
| `[debug] skip_sites=ID,...` | empty | Test bisection: the listed site IDs are resolved but not written. Startup-only. |
| `[debug] diagrams_trace=1` | `0` | Logs chord-diagram recipe loading and movie swaps (with `[chords] diagrams7=1`). Startup-only. |
| `[debug] spin_probe=1` | `0` | A watchdog that samples busy threads (suspends and resumes them briefly). Startup-only. |
| `[debug] plain_delay_ms=N` | `0` | Starts the plain-PSARC patches N ms late (at most 30000), so an enabler with its own plain-PSARC support patches first; RS Extended Range then logs `provided by the CDLC enabler`. Startup-only. |
| `[debug] publish_trace=1` | `0` | Logs the GridBuild readiness waits of the native layouts (a guitar built with 7/8 strings). Startup-only. |
| `[window] background=1` | `0` | The game window never activates itself, and test keys can be sent through the `\\.\pipe\rsxr` pipe ([Overlay (F9)](#overlay-f9)). Startup-only. |
| `[test] fast=1` | `0` | A song ends on its song page instead of the results screen. Startup-only. |
| `[test] force_colourblind=1`, `force_lefty=1`, `force_headstock=33\|6` | `0`, `0`, empty | Test variants for the maintainer's in-game smoke runs (`visual7cb`, `visual7lefty`, `visual7hs33`, `visual7hs6`): runtime overrides of the profile's color mode, handedness and headstock type, never saved ([ARCHITECTURE.md section 11](architecture/11-window-input-overlay-logging.md#11-window-input-overlay-logging)). `-1` (or `off` / `rh`) forces the other way. Startup-only; each logs a `variant:` line when the game applies it. |

Live versus startup-only: you can switch `diagnostics` (D1/D4 only), the color and spacing keys and
`[chords] wide_misses` live in the overlay. Keys that decide which hooks exist, or that size pools at guitar load
(`string7_line`, `background`, ...), are startup-only. Their label ends in ` *`, and a changed one shows "restart required".

## Overlay (F9)

The overlay is a Dear ImGui window with one tab bar below the save bar and the restart note.

- The player tabs come from the `RsxrUiTab::Player` sections of the features' `RsxrUiTable`s (Extra strings, Guitar,
  Tuner, Menu). Each setting has a plain label without key names, and its one-line hint as a `(?)` tooltip. If a
  control is locked, the reason stays visible as text.
- The last tab, Advanced, holds the tabs Status, Colour, Neck, Tuner, Debug, Window and Log. They show the technical
  labels, tooltips with the ini key and the `[live]` / `[next session]` tags.
- `o tab <name>` selects a technical tab (and opens Advanced), `advanced`, or a player tab (`seventh-string`,
  `guitar`, `menu`). Use `player-tuner` for the player Tuner tab, because plain `tuner` is the technical one.
- The default window height is computed from the longest player tab.
- Restart-only keys carry ` *` in their label (one note explains it), and a banner counts the changes that wait for
  a restart.
- The keys with an overlay control are the "F9" column in [SETTINGS.md](release/SETTINGS.md).
- "Save to rsxr.ini" writes only the changed keys.

The overlay renders while the window is unfocused. It takes commands over the `\\.\pipe\rsxr` named pipe, which also
carries the test keys. This needs `[window] background=1`:

```powershell
uv run python tools\rsoverlay.py show | hide | toggle | save | status
uv run python tools\rsoverlay.py tab neck          # status colour neck tuner debug window log
uv run python tools\rsoverlay.py set neck.spacing_guitar7 1.34
```

- Each command logs `overlay: cmd ... -> <live | next session | restart required | invalid value |
  unknown key>`.
- The code is in `src/overlay/`, `src/d3d/present.*`, `src/platform/settings.*` and `src/overlay/overlaycmd.*`.
  `src/platform/settings.*` is the settings store. Each feature's table is in its `settings.cpp` and is listed in
  `src/settings_tables.cpp`.
- The DLL has its own ImGui context (RSMods has another in `xinput1_3.dll`). It has no `imgui.ini`, no
  XInput and no imm32/shell32 imports (`src/overlay/imconfig_rsxr.h`).
- d3d9 sometimes rewrites the game device's vtable (seen at song load). The worker re-checks
  slots 16/17 and 64/65 every second and logs `slots were reset ... hooked again`.

## Site families

`sites.json` is the source of truth, and each entry's `note` explains it. This table lists the ID families, to help
you find your way:

| Prefix | Area |
|---|---|
| S1-S4 | Detection: tuning at song setup, expected pitch. |
| R*, BD*, V2, NG* | Lanes, gems, bends, neck targets (the native next-note grid). |
| N1-N21, GS*, GR*, PS*, PG*, PH* | Seventh string line, ghost, bends, native GuitarStrings and PegSet, peg tint. |
| F1, B1/B2 | Fret bars, chord frames. |
| AL1, X_DDSH, X_NF* | Headstock model merge, texture registration. |
| T*, TS*, TS8*, TU8*, L1/L2, PK/PE | Tuner (legacy phases and the native session copies). |
| TJ, TU/TUT, TN | Tuning names. |
| TP*, CP*, NR*, NS*, CT*, MD*, RQ*, AM*, EV*, BT1/CS1, AG*, UP*, PD*, XB1 | Native N-slot chords and extension block. |
| PN1, JM1, XI1 | Native package format: package magic, manifest text, chart body ([NATIVE-FORMAT.md](NATIVE-FORMAT.md)). |
| FGR*, PUB*, NG*, PA*, PM*, PC*, PE0 | Native FretGrid, publication, next-note grid, pitch. |
| GK* | C-lite guitar build (count, equality, trigger, manager, Lua count, headstock loop clamps). |
| DGM, DGS, DGB, TP13 | Chord diagram data. |
| PP_* | Plain PSARC. |
| SE1-SE3 | Song end log. |
| TVCB, TVGP | Test variants (written only with a `[test] force_*` key on). |
| `X_*` | References, never written: game globals and callees (`ref`, `callref`). |

Counting rule: if a site does not resolve, its feature group is disabled before anything is written. If a core site,
or a member of a unit without a fallback, does not resolve, patching stops altogether ([patch mechanism](architecture/02-patch-mechanism.md)).
Which sites are written depends on the active feature switches. `tools/sitecheck.py` reports the totals.

## Stub layer

`src/features/<name>/hooks_stubs*.cpp` hold the naked stubs, one file per area. Each file starts with a map of its stubs.
A stub is exported as `Rsxr_Stub_<site id>` with its `g_rsxr_resume_<id>` / `g_rsxr_target_<id>` variables. These
names never change, because the registry, `tools/check_exports.py` and the emulator tests find them by name.
The rules every stub keeps are in [chapter 14, "Remaining stubs"](architecture/14-mid-hook-contract-and-cold-site-conversion.md#remaining-stubs).
New sites are mid-hooks (`platform/midhook.h`), not new stubs.

A refactor of the stub files must not change the code. Build the previous commit and the new one, then run:

    uv run python tools\cmp_stub_bytes.py OLD\dinput8.dll NEW\dinput8.dll

It reports `BYTE-IDENTICAL` if all sections are equal (with the debug directory masked). Otherwise it compares every
exported function instruction by instruction, with addresses masked, and reports `SAME INSTRUCTIONS` if the stubs only moved.

## Release

See [RELEASING.md](RELEASING.md) (`scripts/release.ps1`, `scripts/package.ps1`). The
package script uses an allowlist, so it refuses game and Ubisoft file types.
