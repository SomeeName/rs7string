# Code structure and repository layout

Source layers, feature-module rules, size limits, formatting rules and the full path table. They add detail to [AGENTS.md](../../AGENTS.md).

## Code structure

The code is feature modules on a small shared core
([ARCHITECTURE.md section 15](../architecture/15-source-layout.md#15-source-layout)), in these layers:

| Layer | Path | May include |
|---|---|---|
| Platform: patching, site resolve, mid-hook, memory protection, log, config. Knows no game feature. | `src/platform/` | `platform/` |
| Model: the shared string-count model (strings, tunings, the 7-string unit switches). No game addresses. | `src/model/` | `model/`, `platform/` |
| Feature: one game feature (tuner, pegs, chords, psarc, ...): its sites, hooks, logic, settings, registration. | `src/features/<name>/` | its own folder, `platform/`, `model/`, another feature's `features/<x>/<x>.h` only |
| App: startup, proxy, D3D device hooks, overlay. | `src/*.cpp`, `src/d3d/`, `src/overlay/` | everything, other features through `<x>.h` only |

### Separate logic from game hooks

Logic (`<name>.cpp`, `<part>.cpp`) takes values
  and returns values, reads no registers and no game addresses, and has native unit tests. Hooks
  (`hooks.cpp`, `hooks_<part>.cpp`) read registers or game memory, call the logic and write the result back;
  they hold no decisions of their own. A feature's public header `<name>.h` is all other code may use.
### Feature ownership

One feature owns its whole slice: sites, hooks, registration table, settings keys and their defaults,
  overlay controls and status rows (its `Rs7UiTable`, [chapter 15](../architecture/15-source-layout.md) "Overlay tabs"), and tests
  (`tests/<name>/` and `tools/tests/`). Adding a site or a setting touches that feature's folder,
  not a central list.

### Public builds

Private-only features (listed in `.publicignore`) keep their sites in `src/features/<name>/sites.json`
(`tools/sitetable.py` loads them after `sites.json`). Public code reaches them only through the no-op hooks of
`src/features/extras/extras.h` (`RS7_PRIVATE`, `private.props`), and Python tests only through
`tools/private_features.py`. Public files never name them (`tools/tests/test_public_tree.py`).

Both builds must pass: run `build.ps1 -Public` as well when a change touches the public build. The public-tree tests
check that excluded files and references do not enter a release.

### New hooks

Use a C++ mid-hook (`Rs7Mid`) and select the mode for the site's hit rate and callback needs:

| Mode | Use | Callback restriction |
|---|---|---|
| Full | Cold or infrequent sites | General floating-point support |
| Light | Hot, integer-only sites | No x87, MMX or SSE, including in callees |
| SSE | Hot sites needing float math | SSE is allowed; no x87 or MMX, including in callees |

List light and SSE callbacks in `LIGHT_CALLBACKS` or `SSE_CALLBACKS` so the emulator checks them.
For a call site, choose `Before`, `After` or `Wrap` based on when the callback needs to run.
New hand-written `__asm` requires a reason in the stub's header and a baseline update in the same pull request.
See the [mid-hook guide](../architecture/14-mid-hook-contract-and-cold-site-conversion.md) for the contract and measurements.

<details markdown="1">
<summary><b>Mode, placement and assembly details</b></summary>

New hooks are C++ mid-hooks (`Rs7Mid`, a callback taking `Context&`). Mode by cost budget: full mode (237-245
  ns per hit with the guard) where hits per frame x 245 ns stays well under about 20 us (per-frame drivers, a few calls per
  frame, per song / load, once per event). Sites with many hits per frame (per note, gem or mesh, audio thread) use
  the light mode (`Rs7_Mid("ID", &cb).Light()`, 50-51 ns) or, when the callback needs float math, the
  sse mode (`Rs7Mid::Mode::Sse`, 55-59 ns: XMM and MXCSR saved, x87 untouched, an x87 result via
  `Rs7Mid::LoadSt0`), or stay asm. A light callback must not use x87/MMX/SSE, so no floats and no zero-initialised or
  copied structs; an sse callback may use SSE but no x87/MMX (no non-inlined function returning float/double, no CRT
  math). List the site in `LIGHT_CALLBACKS` / `SSE_CALLBACKS` (`tools/tests/emu_harness.py`), whose tests reject such
  instructions ([ARCHITECTURE.md section 14](../architecture/14-mid-hook-contract-and-cold-site-conversion.md#14-mid-hook-contract-and-cold-site-conversion)). On a
  `call rel32` site pick the placement: `Before` (at the callee's entry, may replace it with `Rs7Mid::Ret` /
  `Return`), `After` (after it returned) or `Wrap` (before and after the callee, both seeing its arguments). New
  hand-written `__asm` needs a stated reason in the stub's header comment (custom calling convention the mid-hook
  cannot express) and a baseline update in the same pull request. Do not add stub macros.

</details>

### Size and layout limits

No source file over 800 lines (aim for 600) and no function over 100 lines (aim for 60);
  `tools/check_layout.py` checks both and the include direction above (every layer; a feature may not include
  `d3d/`, `overlay/` or `src/*.h`), and `build.ps1` runs it. Existing
  violations are listed in `tools/layout_baseline.json`: an entry may shrink, never grow, and is removed when
  fixed (`--write-baseline`). A pull request may only shrink the baseline (a moved file keeps its entry under
  the new path).
### Other code rules

- **DRY:** a second copy of a helper, a retry loop or a table is a refactor in the same pull request: move it to
  `platform/` (no game knowledge) or the owning feature. Generated tables come from `tools/gen_*.py`.
- **Faults:** read game memory through `Rs7_SafeCopy` / `Rs7_SafeCopyZero` (`platform/safecopy.h`); a `__try`
  around a call into game or library code uses `__except (Rs7_FaultFilter(GetExceptionCode()))` (`platform/seh.h`),
  never a catch-all, so a stack overflow or a breakpoint is not swallowed. Never hold a lock across either.
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
| `src/platform/` | Shared services: patching, hooks, configuration and logging. |
| `src/model/` | Shared instrument state, tuning and the RS7X extension. |
| `src/features/<name>/` | Feature logic, hooks, settings and registration. See the [source layout](../architecture/15-source-layout.md). |
| `src/proxy.cpp`, `src/dllmain.cpp`, `src/startup*.cpp`, `src/dinput_wrap.cpp` | The `dinput8` proxy, DLL entry and the startup worker that checks the game and runs the patch batches. |
| `src/settings_tables.cpp`, `src/settings_removed.cpp` | The list of every feature's settings table, and the keys removed in 1.0, which startup logs as ignored. |
| `src/registry.cpp`, `src/hook_rules.cpp` | The hook table list and lookup, the failure-group cascade and the write conditions (`src/hook_rules.h`; the groups themselves in `model/groups.h`). |
| `src/d3d/`, `src/overlay/` | Device hooks and the F9 overlay. |
| `sites.json`, `src/generated/` | Patch sites and the tables built from them (`src/generated/` is written by the build). |
| `tests/` | Native unit tests. |
| `fuzz/` | libFuzzer targets of the parsers (`scripts\fuzz.ps1`, `build.ps1 -Fuzz`). |
| `tools/`, `tools/tests/` | Site generators and checks (`gen_*.py`, `sitecheck.py`, `check_exports.py`), the pytest suite (site table and Unicorn emulator tests), `tools/gfxpatch/` (offline chord-diagram movie patcher), `manifest_key.py`, `psarc_plain.py`, `rsoverlay.py`. |
| `scripts/` | Release packaging (`package.ps1`, `release.ps1`), `format.ps1`, `fuzz.ps1`, `compare_builds.ps1` (same machine code check), `pytest_nogame.ps1` (pytest as CI runs it, without game files). |
| `data/` | Headstock build packages and procedural textures (`data/headstock/`, MIT, see its `LICENSE.md`) and chord-diagram recipes (`data/chord-diagrams/`, MIT, see its `LICENSE`); no Ubisoft data. |
| `third_party/imgui/` | Dear ImGui, vendored unmodified (MIT). |
| `docs/release/` | User-facing guides, changelog, RS7X format (the release zip carries this folder). |
| `docs/GETTING-STARTED.md`, `docs/DEVELOPMENT.md`, `docs/ARCHITECTURE.md`, `docs/RELEASING.md` | Developer reference and the release process. |
| `mkdocs.yml`, `site-src/` | The documentation site (see [docs site](docs-and-testing.md#docs-site)). |
