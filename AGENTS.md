# Agent and contributor guide

**Rules and commands for AI coding agents (and humans) working on rs7string.**

[Contributing](CONTRIBUTING.md) · [Development](docs/DEVELOPMENT.md) · [Architecture](docs/ARCHITECTURE.md) · [RS7X spec](docs/release/RS7X-SPEC.md) · [Releasing](docs/release/RELEASING.md) · **Agents**

rs7string is a `dinput8.dll` mod that adds native seven-string guitar support to Rocksmith 2014
Remastered (Steam, September 2022 build, 32-bit Windows). It patches the running game at byte-pattern
sites (`sites.json`) with naked stubs and mid-hooks, and ships no Ubisoft data.

Read first: [README.md](README.md), [CONTRIBUTING.md](CONTRIBUTING.md),
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (build, tools, debug settings) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (how the mod works, decisions, known limits).

## Rules

> [!IMPORTANT]
> - **Work only in `SomeeName/rs7string-private`.** The public `SomeeName/rs7string` repository is read-only; never push or make changes there.
> - **Never commit to `main`.** Work on a branch (`fix/...`, `feat/...`, `docs/...`) and open a pull request.
> - Commit messages and author/co-author fields must not contain AI assistant names or attribution.
> - **Do not push, tag or publish a release** unless the maintainer asked for it.
> - **No Ubisoft data:** no encryption keys, game assets (archives, models, textures, movies, sounds),
>   decrypted content, or disassembly/decompiler listings of the game in the repository, issues or
>   pull requests. Short identifiers, addresses, function names and a few bytes of a pattern are fine.
> - **No generated files** from a user's game install: built `.nif` models, asset dumps, patched movies,
>   memory dumps, game logs with personal paths. `.gitignore` excludes the usual output folders.
> - **Never modify a game folder** from a build or test, never while the game runs, and back up a file
>   before replacing it.

- Tag claims about game behaviour **CONFIRMED** (with address and evidence: disassembly, emulator run
  or in-game log) or **HYPOTHESIS**. Do not present a plausible guess as fact. These tags belong in
  developer docs, not in player docs.

## Build and gates

Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell, [uv](https://docs.astral.sh/uv/).

```powershell
.\build.ps1                                                   # DLL + native unit tests
uv run pytest tools\tests -q                                  # site table + emulator tests
uv run python tools\sitecheck.py                              # every site resolves in the game image
uv run python tools\check_exports.py bin\Release\dinput8.dll  # exports, image base, imports
```

- If you run MSBuild yourself, pass `/p:PlatformToolset=v145` and `/p:PostBuildEventUseInBuild=false`.
  `build.ps1` does both and never copies anything into a game folder.
- Both projects compile at `/W4` with `/WX`: a new compiler warning fails the build. Fix it (no behaviour
  change); do not disable it project-wide. Only where the fix would change behaviour, wrap that code in
  `#pragma warning(push)` / `disable : <code>` / `pop` with a comment saying why. `third_party/` is excluded.
- `build.ps1` ends with MSVC static analysis (`/analyze`, `analyze.props`, `tools/check_analyze.py`): a new
  finding fails the build. Fix it; only a false positive may go into `tools/analyze_baseline.json`, which may
  only shrink in a pull request. `-SkipAnalysis` is for quick local loops only. See
  [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test).
- `.\build.ps1 -Asan` also runs the unit tests under AddressSanitizer (tests only, never the DLL). Run it for
  changes to buffers, parsers or allocation; see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test).
- `.\build.ps1 -Fuzz` also runs the libFuzzer targets of the parsers of untrusted input, 30 s each
  (`scripts\fuzz.ps1`, own project, never the DLL); see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#fuzzing).
- CI (`.github/workflows/ci.yml`, pull requests and `main`) runs the gates that need no game files: build with
  ASan tests, check_exports, pytest without the game image; the `/analyze` job may fail until the hosted MSVC
  matches the baseline's. sitecheck, the emulator tests, `-Fuzz` and in-game runs stay local
  ([docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#continuous-integration)).
- `sitecheck.py` and the emulator tests need a local, unpacked copy of your own game executable
  (`original\Rocksmith2014.analysis.exe` / `.unpacked.exe`, in the folder that contains the checkout).
  Without it those tests skip (CI hides it with `RS7_GAME_FILES`, `tools/game_files.py`); contributors without it
  say so in the pull request. Never commit that file.
- On Windows set `PYTEST_ADDOPTS=-p no:faulthandler` if Unicorn's first-chance exceptions flood the
  output.
- `build.ps1` fails on unformatted sources: run `.\scripts\format.ps1` first (clang-format, ruff; no import
  autofix). See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#formatting).
- Docs-only changes: check that every relative link resolves.

## Adding or removing a hook site

Keep these consistent in one change:

1. **Site:** `sites.json` (id, a pattern that matches exactly once, `expect` bytes, kind, note), with
   `tools/sitecheck.py` passing.
2. **Hook:** the stub or mid-hook and its registration: for a feature in the target layout in
   `src/features/<name>/hooks.cpp` and `registration.cpp` (its hook table, listed once in `kTables` of
   `src/registry.cpp`); a new failure group in `model/groups.h`, its cascade and write conditions in
   `src/hook_rules.cpp`. See [ARCHITECTURE.md section 16](docs/ARCHITECTURE.md#16-source-layout).
3. **Settings:** the feature's settings table (`src/features/<name>/settings.cpp`, listed once in `kRs7SettingTables` of
   `src/settings_tables.cpp`), `docs/release/SETTINGS.md`, and the overlay
   controls if it has a key.
4. **Exports:** `src/proxy.def` / `tools/check_exports.py` when a symbol the emulator tests call changes.
5. **Tests:** a native test in `tests/` (`tests/<name>/` for a moved feature) and an emulator comparison against the stock bytes in `tools/tests/`.
6. **Docs:** `docs/ARCHITECTURE.md`, and `docs/release/CHANGELOG.md` for user-visible changes.

Generated headers (`src/generated/`, `src/features/<name>/*.generated.h`) come from
`tools/gen_*.py`. Regenerate them; do not edit them by hand.

## Inline assembly pitfalls

> [!WARNING]
> An inline-asm operand named like a register or mnemonic is not your C variable: `push gs` pushes the
> GS segment register (it once pushed `2Bh`), `push out` pushes nothing useful. Name operands
> `gsPtr`, `outPtr`, never `gs`, `out`, `in`, `ds`, ...

- MSVC warning C4405 reports this class of bug and is an error here (`TreatSpecificWarningsAsErrors`).
  Do not disable it.
- Naked stubs must preserve every register and flag the stock code relies on, including x87/SSE state
  when calling C++ (use the mid-hook thunk for new sites), and must replay the displaced instructions
  exactly. The emulator tests exist to catch deviations.

## Code structure

The target layout is feature modules on a small shared core
([ARCHITECTURE.md section 16](docs/ARCHITECTURE.md#16-source-layout)). The old layout (`src/hooks/`, `src/core/`)
is gone; all code is in the layers below.

| Layer | Path | May include |
|---|---|---|
| Platform: patching, site resolve, mid-hook, memory protection, log, config. Knows no game feature. | `src/platform/` | `platform/` |
| Model: the shared string-count model (strings, tunings, the 7-string unit switches). No game addresses. | `src/model/` | `model/`, `platform/` |
| Feature: one game feature (tuner, pegs, chords, psarc, ...): its sites, hooks, logic, settings, registration. | `src/features/<name>/` | its own folder, `platform/`, `model/`, another feature's `features/<x>/<x>.h` only |
| App: startup, proxy, D3D device hooks, overlay. | `src/*.cpp`, `src/d3d/`, `src/overlay/` | everything, other features through `<x>.h` only |

- **Inside a feature, logic and game glue are separate files.** Logic (`<name>.cpp`, `<part>.cpp`) takes values
  and returns values, reads no registers and no game addresses, and has native unit tests. Hooks
  (`hooks.cpp`, `hooks_<part>.cpp`) read registers or game memory, call the logic and write the result back;
  they hold no decisions of their own. A feature's public header `<name>.h` is all other code may use.
- **One feature owns its whole slice:** sites, hooks, registration table, settings keys and their defaults,
  overlay controls and status rows (its `Rs7UiTable`, ARCHITECTURE.md section 16 "Overlay tabs"), and tests
  (`tests/<name>/`, `tools/tests/<name>/` once moved). Adding a site or a setting touches that feature's folder,
  not a central list.
- **New hooks are C++ mid-hooks** (`Rs7Mid`, a callback taking `Context&`). Mode by cost budget: full mode (about
  150 ns per hit) where hits per frame x 150 ns stays well under about 20 us (per-frame drivers, a few calls per
  frame, per song / load, once per event). Sites with many hits per frame (per note, gem or mesh, audio thread) use
  the light mode (`Rs7Hook::midMode = Rs7Mid::Mode::Light`, about 25 ns) or, when the callback needs float math, the
  sse mode (`Rs7Mid::Mode::Sse`, about 20-30 ns: XMM and MXCSR saved, x87 untouched, an x87 result via
  `Rs7Mid::LoadSt0`), or stay asm. A light callback must not use x87/MMX/SSE, so no floats and no zero-initialised or
  copied structs; an sse callback may use SSE but no x87/MMX (no non-inlined function returning float/double, no CRT
  math). List the site in `LIGHT_CALLBACKS` / `SSE_CALLBACKS` (`tools/tests/emu_harness.py`), whose tests reject such
  instructions ([ARCHITECTURE.md section 15](docs/ARCHITECTURE.md#15-mid-hook-contract-and-cold-site-conversion)). On a
  `call rel32` site pick the placement: `Before` (at the callee's entry, may replace it with `Rs7Mid::Ret` /
  `Return`), `After` (after it returned) or `Wrap` (before and after the callee, both seeing its arguments). New
  hand-written `__asm` needs a stated reason in the stub's header comment (custom calling convention the mid-hook
  cannot express) and a baseline update in the same pull request. The remaining stubs and what is planned for them
  are in the maintainer's handover notes (`docs/handover.md`, private repository only); do not add stub macros.
- **Size limits:** no source file over 800 lines (aim for 600) and no function over 100 lines (aim for 60);
  `tools/check_layout.py` checks both and the include direction above (every layer; a feature may not include
  `d3d/`, `overlay/` or `src/*.h`), and `build.ps1` runs it. Existing
  violations are listed in `tools/layout_baseline.json`: an entry may shrink, never grow, and is removed when
  fixed (`--write-baseline`). A pull request may only shrink the baseline (a moved file keeps its entry under
  the new path).
- **DRY:** a second copy of a helper, a retry loop or a table is a refactor in the same pull request: move it to
  `platform/` (no game knowledge) or the owning feature. Generated tables come from `tools/gen_*.py`.
- **No god objects:** a file or class has one responsibility you can name in a few words; global state lives in
  one state struct per feature, not in loose globals spread over files.
- **Every larger feature ends with a short cleanup step.** Reviews check readability, not only bugs.
- **Braces:** every `if` / `else` / `for` / `while` / `do` body is braced, also a one-line body (clang-format
  `InsertBraces`, applied by `scripts\format.ps1`).
- **Blank lines:** `tools/blank_lines.py` (run by `scripts\format.ps1`, checked by `build.ps1`) puts exactly one
  blank line between two neighbouring statements of a function or lambda body when
  1. either is a multi-line control block: `for` / `while` / `do` / `switch` / `try`, a bare `{ }` block, or an
     `if` / `else` with a branch of more than one statement or more than two lines;
  2. one is an `__asm` block or statement and the other is not (a run of `__asm` statements is one unit);
  3. the first ends a run of declarations and the second is not a declaration;
  4. the first ends a run of short guard clauses (`if (...) { return/continue/break/goto/throw ...; }`, one
     statement, at most two lines, no `else`) and the second is not a guard;
  5. the second is the final `return` of the function body and something precedes it.

  Guard clauses are not surrounded by blank lines (also not after a declaration: the guard checks it). Never a
  blank line at the start or end of a block, never two in a row, and a comment stays attached to the line
  below it (the blank line goes above the comment). The tool never edits inside `__asm` blocks, macros,
  strings, initializer lists or a function it cannot parse cleanly. Do not fight it: restructure the code, or
  put a comment line between two statements you want kept together.
- **A style or layout change must not change the code.** Compare every function of two builds with
  `scripts\compare_builds.ps1 -Old <rev> -New <rev>` (linker maps, `tools/cmp_functions.py`).

## Layout

| Path | Contents |
|---|---|
| `src/platform/`, `src/model/`, `src/features/<name>/` | Target layout, see "Code structure". `src/platform/` holds patch, pattern, resolve, protect, sites, midhook, fncopy, log, retry, safecopy, config (ini helpers), settings (the settings store), uilayout (the overlay tab items), freeze (thread freeze for patching), dxt, spinprobe, hooktable (`Rs7Hook` / `Rs7HookTable` and their lookup) and isolate (the patch rounds with the app's group policy); `src/features/psarc/`, `src/features/vguit/`, `src/features/tuner/`, `src/features/colours/`, `src/features/chords/`, `src/features/diagrams/`, `src/features/headstock/`, `src/features/publish/`, `src/features/fretgrid/`, `src/features/nextgrid/`, `src/features/frets/`, `src/features/strings/`, `src/features/scene/`, `src/features/string7/`, `src/features/pitch/`, `src/features/nslot/`, `src/features/clite/`, `src/features/allocstats/`, `src/features/diag/`, `src/features/songend/`, `src/features/testdrive/` and `src/features/window/` are moved; `src/model/` holds eight, tuning, stringcolour, extblock (the RS7X format), status (the overlay's status board) and groups (the failure groups, the 7-string unit). |
| `src/registry.cpp`, `src/hook_rules.cpp` | The hook table list and lookup, the failure-group cascade and the write conditions (`src/hook_rules.h`; the groups themselves in `model/groups.h`). |
| `src/d3d/`, `src/overlay/` | Device hooks and the F9 overlay. |
| `sites.json`, `src/generated/` | Patch sites and the tables built from them. |
| `tests/` | Native unit tests. |
| `fuzz/` | libFuzzer targets of the parsers (`scripts\fuzz.ps1`, `build.ps1 -Fuzz`). |
| `tools/`, `tools/tests/` | Generators, checks, emulator and site tests. |
| `data/` | Headstock build packages and chord-diagram recipes (MIT, no Ubisoft data). |
| `docs/release/` | User-facing guides, changelog, RS7X format, release process. |
| `docs/DEVELOPMENT.md`, `docs/ARCHITECTURE.md` | Developer reference. |
| `docs/handover.md` | Private repository only: state of the refactor, what remains, how to run every gate. |

## Docs style

Player docs (`README.md`, `docs/release/` except `RS7X-SPEC.md` and `RELEASING.md`):

- A header block (title, one-line purpose, navigation line), then simple explanations first and
  details later. Tables for settings, requirements and known issues; numbered steps for procedures.
- GitHub callouts (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`) and `<details>` for long or
  advanced parts.
- Terms: seven-string, low B, CDLC enabler, plain PSARC, headstock builder.
- No internal addresses, no CONFIRMED/HYPOTHESIS tags, no personal or workspace paths. "C-lite" only
  in developer docs.
- `docs/release/SETTINGS.md` must list exactly the keys and defaults that
  `scripts\package.ps1 -TemplateOnly` prints.

## In-game testing

Test only with your own legally owned Rocksmith 2014 Remastered (Steam, September 2022 build), with
plain (unencrypted) PSARC test charts made for rs7string, on a copy of the files you can restore.

- Do not test while changing game files.
- Do not ship or post game screenshots, logs with personal paths, models or other results derived
  from the game's data.
- Include relevant `rs7string.log` lines in bug reports.
