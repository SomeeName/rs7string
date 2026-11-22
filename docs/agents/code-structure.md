# Code structure and repository layout

This page covers the source layers, the rules for feature modules, the size limits, the formatting rules and the full path table. It adds detail to [AGENTS.md](../../AGENTS.md).

## Code structure

The code is made of feature modules on top of a small shared core
([ARCHITECTURE.md section 15](../architecture/15-source-layout.md#15-source-layout)). It has these layers:

| Layer | Path | May include |
|---|---|---|
| Platform: patching, site resolve, mid-hook, memory protection, log, config. Knows no game feature. | `src/platform/` | `platform/` |
| Model: the shared string-count model (strings, tunings, the 7-string unit switches). No game addresses. | `src/model/` | `model/`, `platform/` |
| Feature: one game feature (tuner, pegs, chords, psarc, ...): its sites, hooks, logic, settings, registration. | `src/features/<name>/` | its own folder, `platform/`, `model/`, another feature's `features/<x>/<x>.h` only |
| App: startup, proxy, D3D device hooks, overlay. | `src/*.cpp`, `src/d3d/`, `src/overlay/` | everything, other features through `<x>.h` only |

### Separate logic from game hooks

Logic code (`<name>.cpp`, `<part>.cpp`) takes values and returns values. It reads no registers and no game
addresses, and it has native unit tests. Hook code (`hooks.cpp`, `hooks_<part>.cpp`) reads registers or game
memory, calls the logic and writes the result back. Hooks make no decisions of their own. Other code may use
only a feature's public header, `<name>.h`.

### Feature ownership

Each feature owns everything that belongs to it: sites, hooks, registration table, settings keys and their
defaults, overlay controls and status rows (its `RsxrUiTable`, [chapter 15](../architecture/15-source-layout.md) "Overlay tabs"),
and tests (`tests/<name>/` and `tools/tests/`). When you add a site or a setting, you change that feature's folder,
not a central list.

### Public builds

Private-only features (listed in `.publicignore`) keep their sites in `src/features/<name>/sites.json`.
`tools/sitetable.py` loads them after `sites.json`. Public code reaches these features only through the no-op hooks
of `src/features/extras/extras.h` (`RSXR_PRIVATE`, `private.props`). Python tests reach them only through
`tools/private_features.py`. Public files never name them, and `tools/tests/test_public_tree.py` checks this.

Both builds must pass. If a change touches the public build, run `build.ps1 -Public` as well. The public-tree tests
check that excluded files and references do not get into a release.

### New hooks

Use a C++ mid-hook (`RsxrMid`). Choose the mode that fits how often the site runs and what the callback needs:

| Mode | Use | Callback restriction |
|---|---|---|
| Full | Cold or infrequent sites | General floating-point support |
| Light | Hot, integer-only sites | No x87, MMX or SSE, including in callees |
| SSE | Hot sites needing float math | SSE is allowed; no x87 or MMX, including in callees |

List light and SSE callbacks in `LIGHT_CALLBACKS` or `SSE_CALLBACKS`, so that the emulator checks them.
For a call site, choose `Before`, `After` or `Wrap` based on when the callback needs to run.
New hand-written `__asm` needs a reason in the stub's header and a baseline update in the same pull request.
See the [mid-hook guide](../architecture/14-mid-hook-contract-and-cold-site-conversion.md) for the contract and measurements.

<details markdown="1">
<summary><b>Mode, placement and assembly details</b></summary>

New hooks are C++ mid-hooks (`RsxrMid`, a callback that takes `Context&`). Choose the mode by its cost:

- **Full mode** costs 237-245 ns per hit, with the guard. Use it where hits per frame x 245 ns stays well under
  about 20 us: per-frame drivers, a few calls per frame, once per song or load, once per event.
- **Light mode** (`Rsxr_Mid("ID", &cb).Light()`, 50-51 ns) is for sites with many hits per frame: per note, gem or
  mesh, or on the audio thread.
- **SSE mode** (`RsxrMid::Mode::Sse`, 55-59 ns) is for such sites when the callback needs float math. It saves XMM
  and MXCSR and leaves x87 untouched. Return an x87 result with `RsxrMid::LoadSt0`.
- Otherwise, keep the site in asm.

A light callback must not use x87/MMX/SSE. This means no floats and no zero-initialized or copied structs. An sse
callback may use SSE but not x87/MMX: no non-inlined function that returns float/double, and no CRT math. List the
site in `LIGHT_CALLBACKS` / `SSE_CALLBACKS` (`tools/tests/emu_harness.py`). Their tests reject such
instructions ([ARCHITECTURE.md section 14](../architecture/14-mid-hook-contract-and-cold-site-conversion.md#14-mid-hook-contract-and-cold-site-conversion)).

On a `call rel32` site, choose the placement:

- `Before` runs at the callee's entry and may replace the callee with `RsxrMid::Ret` / `Return`.
- `After` runs after the callee has returned.
- `Wrap` runs before and after the callee, and both calls see its arguments.

New hand-written `__asm` needs a stated reason in the stub's header comment. The usual reason is a custom calling
convention that the mid-hook cannot express. It also needs a baseline update in the same pull request. Do not add stub macros.

</details>

### Size and layout limits

No source file may be over 800 lines (aim for 600), and no function over 100 lines (aim for 60).
`tools/check_layout.py` checks both limits. It also checks the include direction from the table above for every
layer. For example, a feature may not include `d3d/`, `overlay/` or `src/*.h`. `build.ps1` runs this check.

`tools/layout_baseline.json` lists the existing violations. An entry may shrink but never grow, and you remove it
when you fix the violation (`--write-baseline`). A pull request may only shrink the baseline. A moved file keeps
its entry under the new path.

### Other code rules

- **DRY (do not repeat yourself):** if you would add a second copy of a helper, a retry loop or a table, refactor
  in the same pull request instead. Move the code to `platform/` (no game knowledge) or to the feature that owns it.
  Generated tables come from `tools/gen_*.py`.
- **Faults:** read game memory through `Rsxr_SafeCopy` / `Rsxr_SafeCopyZero` (`platform/safecopy.h`). A `__try`
  around a call into game or library code uses `__except (Rsxr_FaultFilter(GetExceptionCode()))` (`platform/seh.h`),
  never a catch-all, so that a stack overflow or a breakpoint is not swallowed. Never hold a lock across either.
- **No god objects:** a file or class has one responsibility that you can name in a few words. Global state lives in
  one state struct per feature, not in loose globals spread over files.
- **Every larger feature ends with a short cleanup step.** Reviews check readability, not only bugs.
- **Braces:** every `if` / `else` / `for` / `while` / `do` body is braced, also a one-line body (clang-format
  `InsertBraces`, applied by `scripts\format.ps1`).
- **Blank lines:** `tools/blank_lines.py` (run by `scripts\format.ps1`, checked by `build.ps1`) puts exactly one
  blank line between two neighboring statements of a function or lambda body when
  1. either is a multi-line control block: `for` / `while` / `do` / `switch` / `try`, a bare `{ }` block, or an
     `if` / `else` with a branch of more than one statement or more than two lines;
  2. one is an `__asm` block or statement and the other is not (a run of `__asm` statements is one unit);
  3. the first ends a run of declarations and the second is not a declaration;
  4. the first ends a run of short guard clauses (`if (...) { return/continue/break/goto/throw ...; }`, one
     statement, at most two lines, no `else`) and the second is not a guard;
  5. the second is the final `return` of the function body and something precedes it.

  Guard clauses get no blank lines around them. This also holds after a declaration, because the guard checks it.
  A block never starts or ends with a blank line, and there are never two blank lines in a row. A comment stays
  attached to the line below it, so the blank line goes above the comment. The tool never edits inside `__asm`
  blocks, macros, strings, initializer lists or a function it cannot parse cleanly. Do not fight the tool.
  Restructure the code instead, or put a comment line between two statements that you want to keep together.
- **A style or layout change must not change the code.** Compare every function of two builds with
  `scripts\compare_builds.ps1 -Old <rev> -New <rev>` (linker maps, `tools/cmp_functions.py`).

## Layout

| Path | Contents |
|---|---|
| `src/platform/` | Shared services: patching, hooks, configuration and logging. |
| `src/model/` | Shared instrument state, tuning and the extension block. |
| `src/features/<name>/` | Feature logic, hooks, settings and registration. See the [source layout](../architecture/15-source-layout.md). |
| `src/proxy.cpp`, `src/dllmain.cpp`, `src/startup*.cpp`, `src/dinput_wrap.cpp` | The `dinput8` proxy, DLL entry and the startup worker that checks the game and runs the patch batches. |
| `src/settings_tables.cpp`, `src/settings_removed.cpp` | The list of every feature's settings table, and the keys removed in 1.0, which startup logs as ignored. |
| `src/registry.cpp`, `src/hook_rules.cpp` | The hook table list and lookup, the failure-group cascade and the write conditions (`src/hook_rules.h`; the groups themselves in `model/groups.h`). |
| `src/d3d/`, `src/overlay/` | Device hooks and the F9 overlay. |
| `sites.json`, `src/generated/` | Patch sites and the tables built from them (`src/generated/` is written by the build). |
| `tests/` | Native unit tests. |
| `fuzz/` | libFuzzer targets of the parsers (`scripts\fuzz.ps1`, `build.ps1 -Fuzz`). |
| `tools/`, `tools/tests/` | Site generators and checks (`gen_*.py`, `sitecheck.py`, `check_exports.py`), the pytest suite (site table and Unicorn emulator tests), `tools/gfxpatch/` (offline chord-diagram movie patcher), `psarc_plain.py`, `rsoverlay.py`. |
| `scripts/` | Release packaging (`package.ps1`, `release.ps1`), `format.ps1`, `fuzz.ps1`, `compare_builds.ps1` (same machine code check), `pytest_nogame.ps1` (pytest as CI runs it, without game files). |
| `data/` | Headstock build packages and procedural textures (`data/headstock/`, MIT, see its `LICENSE.md`) and chord-diagram recipes (`data/chord-diagrams/`, MIT, see its `LICENSE`); no Ubisoft data. |
| `third_party/imgui/` | Dear ImGui, vendored unmodified (MIT). |
| `docs/release/` | User-facing guides and changelog (the release zip carries this folder). |
| `docs/GETTING-STARTED.md`, `docs/DEVELOPMENT.md`, `docs/ARCHITECTURE.md`, `docs/RELEASING.md` | Developer reference and the release process. |
| `mkdocs.yml`, `site-src/` | The documentation site (see [docs site](docs-and-testing.md#docs-site)). |
