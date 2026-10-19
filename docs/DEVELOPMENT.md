# rs7string developer guide

**Build, test and tooling notes for rs7string.**

[Contributing](../CONTRIBUTING.md) · **Development** · [Architecture](ARCHITECTURE.md) · [RS7X spec](release/RS7X-SPEC.md) · [Releasing](release/RELEASING.md) · [Agents](../AGENTS.md)

How the mod works is in [ARCHITECTURE.md](ARCHITECTURE.md); player documentation is in
[docs/release](release/README.md); contribution rules are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Contents

- [Layout](#layout)
- [Build and test](#build-and-test)
- [Continuous integration](#continuous-integration)
- [Fuzzing](#fuzzing)
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
| `src/` | The DLL: `platform/` (patching, site resolve, mid-hook, memory protection, log: no game knowledge), `model/` (the string-count model), `features/<name>/` (one folder per feature), `registry.cpp` / `hook_rules.cpp` (the hook table list, failure groups, write conditions), `d3d/` (device hooks), `overlay/` (Dear ImGui UI). |
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

CI ([Continuous integration](#continuous-integration)) runs only the gates that need no game files; run the rest
locally before every pull request (the commands above).

What `build.ps1` does:

- Fails first when a source file is not formatted (`scripts\format.ps1 -Check`; `-SkipFormatCheck` skips it).
  Fix with `.\scripts\format.ps1` (clang-format for `src/` and `tests/`, ruff for `tools/` and `scripts/`;
  `third_party/` and `*.generated.h` are never formatted). It needs `clang-format` (on PATH or in Visual Studio).
- Regenerates `src/generated/sites.generated.h` from `sites.json`.
- Passes `/p:PlatformToolset=v145` and `/p:PostBuildEventUseInBuild=false`.
- Compiles both projects at `/W4` with warnings as errors (`/WX`, `TreatWarningAsError` in both `.vcxproj`
  files), so any new compiler warning fails the build. `third_party/` (Dear ImGui) compiles with warnings off.
  Fix a warning instead of disabling it; where a fix would change behaviour (an intentional `alignas`
  padding, for example), disable that one code with `#pragma warning(push/disable/pop)` around the
  code and a comment that says why.
- Copies the headstock packages and the `chord-diagrams` folder next to the DLL.
- Runs MSVC static analysis last: both projects compile again with `/analyze` (`analyze.props`, switched on by
  `/p:Rs7Analyze=true`, its own `obj\analyze` folder; `src/` and `tests/`, not `third_party/`), then
  `tools\check_analyze.py` compares the findings (keyed `file::function::Cnnnn`) with
  `tools\analyze_baseline.json`, which may only shrink, like the layout baseline. Rules: the Build Tools'
  default native set (null derefs, buffer overruns, uninitialised reads, API contracts, leaks), minus C6320 /
  C6322 (the deliberate catch-all `__except` guards) and C28159 (`GetTickCount64` advice). About 2 minutes from
  clean, seconds incrementally; `-SkipAnalysis` skips it for a quick local loop, not for a pull request. Fix a new
  finding; only a false positive goes into the baseline (`--write-baseline --allow-grow`, then fill in its
  `note`: why it is a false positive; plain `--write-baseline` only drops fixed entries).
- With `-Asan` (optional, off by default): builds the unit tests again with AddressSanitizer (`asan.props`,
  `/p:Rs7Asan=true`, own `obj\asan` and `bin\Asan` folders; the DLL is never built with ASan), copies the x86 ASan
  runtime from the MSVC toolset next to the exe and runs it; an ASan report (heap/stack overflow, use after free)
  or a failing test fails the build. About a minute. A test whose inline asm uses EBX (ASan's frame pointer,
  C5073) is declared with `TEST_NO_ASAN` instead of `TEST`. Run it before a pull request that touches memory
  handling.
  With `-NoTests` the normal test run is skipped, the ASan tests still run.
- `-Toolset <vNNN>` builds with another platform toolset; `-Toolset auto` takes `v145` when installed, else the
  newest (CI). Without the VS 2026 Build Tools path, MSBuild comes from `vswhere`.
- With `-Fuzz` (optional): afterwards every libFuzzer target of the parsers runs 30 s (`scripts\fuzz.ps1`, own
  project and `bin\Fuzz`; see [Fuzzing](#fuzzing)).
- Never touches a game folder.
- Output: `bin\Release\dinput8.dll` and `bin\Release\rs7string_tests.exe`.

The unit tests include real-device D3D9 tests (`test_d3d_*.cpp`, `test_gpu.cpp`: a hidden window, HAL
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

## Continuous integration

`.github/workflows/ci.yml` runs on pull requests and pushes to `main` (windows-latest) every gate that needs no
game files: `build.ps1 -Toolset auto -Asan -SkipAnalysis` (format and layout check, the Release Win32 build, the
native unit tests, the unit tests under ASan), `tools\check_exports.py`, and `pytest tools\tests` with
`RS7_GAME_FILES` pointing at an empty folder. `tools/game_files.py` resolves the user's own files (unpacked and
analysis exe in `original/`, the memory snapshot in `rskeys/snapshot`, research ledgers) from that folder, by
default the one that contains the checkout; a test that needs one skips without it (`emu_common.needs_exe`, the
emulator's `needs_emu`; the exe and snapshot loaders skip the whole module when it reads them while pytest imports
it). The same check locally: `.\scripts\pytest_nogame.ps1` (sets the variable for that call only). The pytest
header prints the game-files root and whether the exe and the snapshot were found.
The static analysis gate runs in a separate job that may fail: its baseline was recorded with MSVC 14.51 and the
hosted images ship another MSVC; make it required once they match. `sitecheck.py` and the emulator tests run only
locally. clang-format is pinned to the version the sources are formatted with.

## Fuzzing

The parsers of untrusted input have libFuzzer targets (`fuzz\`, one exe, `bin\Fuzz\rs7string_fuzz.exe`), built with
the MSVC Build Tools' own libFuzzer and AddressSanitizer for x86 (`/fsanitize=address /fsanitize=fuzzer`). The fuzz
project is separate from the default build and its tests.

| Target | Input |
|---|---|
| `rs7x` | RS7X extension block (`model/extblock`), with the song's template / ChordNotes / tuning counts |
| `dxt` | DXT1 / DXT5 decoder (`platform/dxt`): sizes, pitch, short buffers, size overflow |
| `settings` | `Rs7Set_SetText` for every key, then `Rs7Set_Format` |
| `ini` | the bytes as `rs7string.ini`: every feature's startup read and `Rs7Set_InitFromIni` |
| `text` | pipe commands, hotkeys, DirectInput key lines, colours, `fret_halflen`, atlas row, vguitar mode / filter / float |
| `package` | headstock build package manifest (`rs7hs::Manifest`) |
| `nif` | texture names in a NIF (`Rs7Asset_DdsNames`) |
| `dds` | DXT1 darkening of a `.dds` (`Rs7Asset_ScaleDxt1`) |
| `recipe` | chord-diagram recipe against a movie (`rs7diagram::SwapMovie`; the fuzz build skips its SHA-256 checks) |

```powershell
.\build.ps1 -Fuzz                                    # the normal build, then every target 30 s
.\scripts\fuzz.ps1 -Seconds 3600 -Target rs7x,recipe # longer runs; -NoBuild reuses the exe
```

The corpus grows in `obj\fuzz\corpus\<target>` across runs; seeds (built in, plus the shipped headstock packages and
recipes) are in `obj\fuzz\seeds\<target>`; findings land in `obj\fuzz\artifacts\<target>\`. Replay one with
`$env:RS7_FUZZ_TARGET = '<target>'; bin\Fuzz\rs7string_fuzz.exe -artifact_prefix=obj\fuzz\artifacts\<target>\ <file>`
(without the prefix libFuzzer writes into the current folder; `.gitignore` covers `crash-*` and friends). A fixed seed (`-seed=1`) keeps the short
run reproducible. Each fixed finding gets a native regression test with the input in `tests/`.

## Formatting

`.clang-format`, `[tool.ruff]` in `pyproject.toml` and `.editorconfig` define the style (LF line endings,
120 columns). Run `.\scripts\format.ps1` before you commit; `build.ps1` fails on unformatted sources.
Lint autofix never touches imports (the emulator tests import pytest fixtures that look unused).
Wrap code that clang-format would break in `// clang-format off` / `// clang-format on` with a reason
(for example the include order of `tests/test_d3d_hook.cpp`). The format run is clang-format (which also adds
braces), then `tools\blank_lines.py` (blank lines between statements, rules in [AGENTS.md](../AGENTS.md#code-structure);
it parses with tree-sitter-cpp, a dev dependency installed by `uv run`), then clang-format again.

`build.ps1` then runs `tools\check_layout.py`: file and function size limits, inline assembly and the include
direction of the source layers ([AGENTS.md](../AGENTS.md#code-structure)). Known violations are listed in
`tools/layout_baseline.json`; after fixing one, run `uv run python tools\check_layout.py --write-baseline`.

After a formatting run, check that nothing changed in the code: `.\scripts\compare_builds.ps1 -Old <rev> -New <rev>`
builds both revisions with a linker map and compares every function of `dinput8.dll` and the test exe
(`tools\cmp_functions.py`: calls and data references compare by symbol name, so moved code is equal), and
`tools\cmp_stub_bytes.py` does the quicker exported-stub check (see [Stub layer](#stub-layer)).

When files are moved or split, add `-NoOpt`: both revisions are built with `/Od /Ob0` (no optimisation, no
inlining; cached in `..\rs7-cmp-out\<sha>-noopt`), because in the Release build a split changes inlining and
frame layout. Symbol names that move with the source path (anonymous namespace, string literals) are
normalised, so a pure move compares equal and a changed function body is still reported. `-Alias <namespace>`
counts a named namespace as the anonymous one, for code moved out of `namespace { }`. The result proves the
code is unchanged, not that the optimised build is.

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
| `[debug] alloc_stats=1` | One `allocstats` line per song setup and song end with process private bytes, working set, free address space (32-bit game), handle count and `kind=live/total` counters for every allocation type the DLL owns (`src/features/allocstats/allocstats.*`). Over several songs the "song start" lines must be flat in every `live` value; `merges`, `textures`, `bindalt`, `pegCtors` count objects the game keeps for the life of the process and may rise once per distinct headstock model. |
| `[debug] tuner_autotune=N` | Reports each string tuned N seconds after selection (test driving without a guitar). |
| `[debug] skip_sites`, `veh_log`, `spin_probe`, `log_redact`, `publish_trace`, `plain_delay_ms`, `diagrams_trace` | See SETTINGS.md; startup-only. |
| `[test] vguitar=1` | Virtual guitarist ([ARCHITECTURE.md section 12](ARCHITECTURE.md#12-virtual-guitarist-test-mode-test-vguitar1)). Keys: `vguitar_mode` (`chart` \| `game` \| `midi:N` \| `off`), `vguitar_filter` (`all` \| strings \| `string:fret`), `vguitar_lead_ms` (40 centres detection), `vguitar_gain`, `vguitar_mix`, `vguitar_trace`, `vguitar_chord6` (`play` \| `omit` \| `only` \| `+N`/`-N` \| `omitlow` \| `omithigh`). All but `vguitar` are re-read at each song start. |
| `[test] force_colourblind=1`, `force_lefty=1`, `force_headstock=33\|6` | Test variants for `scripts/smoke.py` (`visual7cb`, `visual7lefty`, `visual7hs33`, `visual7hs6`): runtime overrides of the profile's colour mode, handedness and headstock type, never saved ([ARCHITECTURE.md section 11](ARCHITECTURE.md#11-window-input-overlay-logging)). Startup-only; each logs a `variant:` line when the game applies it. |

Live versus startup-only: `diagnostics` (D1/D4 only), the colour and neck keys
are live-switchable in the overlay. Keys that decide which hooks exist or size pools at guitar load
(`ramp_swap`, `string7_line`, `headstock_peg`, `background`, ...) are startup-only: their label ends in ` *`, and
a changed one shows "restart required".

## Overlay (F9)

A Dear ImGui window with one tab bar below the save bar and the restart note. The player tabs come from the
`Rs7UiTab::Player` sections of the features' `Rs7UiTable`s (Seventh string, Guitar, Tuner, Menu); each setting has a
plain label, no key names, and its one-line hint as a `(?)` tooltip (the reason a control is locked stays visible as
text). The last tab, Advanced, holds the tabs Status, Colour, Neck, Tuner, Debug, Window and Log with the technical
labels, tooltips with the ini key and the `[live]` / `[next session]` tags. `o tab <name>` selects a technical tab
(and opens Advanced), `advanced`, or a player tab (`seventh-string`, `guitar`, `menu`; `player-tuner` for the player
Tuner tab, since plain `tuner` is the technical one). The default window height is computed from the longest player
tab. Restart-only keys carry ` *` in their label
(one note explains it), and a banner counts changes that wait for a restart. Keys with an overlay control are the
"F9" column in [SETTINGS.md](release/SETTINGS.md). "Save to rs7string.ini" writes only the changed keys.

The overlay renders while the window is unfocused and takes commands from a DirectInput pipe (needs
`[window] background=1`):

```powershell
uv run python tools\rsoverlay.py show | hide | toggle | save | status
uv run python tools\rsoverlay.py tab neck          # status colour neck tuner debug window log
uv run python tools\rsoverlay.py set neck.string7_v 0.5
```

- Each command logs `overlay: cmd ... -> <live | next session | restart required | invalid value |
  unknown key>`.
- Wiring: `src/overlay/`, `src/d3d/present.*`, `src/platform/settings.*` (the store; each feature's table in its `settings.cpp`, listed in `src/settings_tables.cpp`), `src/overlay/overlaycmd.*`.
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
| TVCB, TVGP | Test variants (written only with a `[test] force_*` key on). |
| `X_*` | References, never written: game globals and callees (`ref`, `callref`). |

Counting rule: all sites resolve or none are written; which sites are written depends on the active
feature switches. `tools/sitecheck.py` reports the totals.

The complete prefix glossary (every family, with counts) is in the maintainer's historical refactor plan, which
predates the mid-hook conversion.

## Stub layer

`src/features/<name>/hooks_stubs*.cpp` hold the naked stubs, one file per area; each file starts with a map of its stubs.
A stub is exported as `Rs7_Stub_<site id>` with its `g_rs7_resume_<id>` / `g_rs7_target_<id>` variables; these
names never change (the registry, `tools/check_exports.py` and the emulator tests find them by name).
The rules every stub keeps are in [ARCHITECTURE.md](ARCHITECTURE.md) section 15 "Remaining stubs" (there is no shared
stub-macro header any more). New sites should be mid-hooks (`platform/midhook.h`), not new stubs;
the stubs that remain and the plan for them are in the maintainer's handover notes.

A refactor of the stub files must not change the code. Build the previous commit and the new one and run

    uv run python tools\cmp_stub_bytes.py OLD\dinput8.dll NEW\dinput8.dll

It reports `BYTE-IDENTICAL` (all sections equal, debug directory masked) or compares every exported function
instruction by instruction with addresses masked (`SAME INSTRUCTIONS` when stubs only moved).

## Release

See [release/RELEASING.md](release/RELEASING.md) (`scripts/release.ps1`, `scripts/package.ps1`). The
package script refuses game and Ubisoft file types by an allowlist.
