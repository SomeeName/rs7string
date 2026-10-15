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
> - **Never commit to `main`.** Work on a branch (`fix/...`, `feat/...`, `docs/...`) and open a pull request.
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
- `sitecheck.py` and the emulator tests need a local, unpacked copy of your own game executable
  (`original\Rocksmith2014.analysis.exe` / `.unpacked.exe`, in the folder that contains the checkout).
  Contributors without it may skip those and must say so in the pull request. Never commit that file.
- On Windows set `PYTEST_ADDOPTS=-p no:faulthandler` if Unicorn's first-chance exceptions flood the
  output.
- `build.ps1` fails on unformatted sources: run `.\scripts\format.ps1` first (clang-format, ruff; no import
  autofix). See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#formatting).
- Docs-only changes: check that every relative link resolves.

## Adding or removing a hook site

Keep these consistent in one change:

1. **Site:** `sites.json` (id, a pattern that matches exactly once, `expect` bytes, kind, note), with
   `tools/sitecheck.py` passing.
2. **Hook:** the stub or mid-hook in `src/hooks/` and its registration in `src/hooks/registry.cpp`
   (feature switch, isolation/cascade behaviour, write conditions).
3. **Settings:** `src/core/settings.*`, `docs/release/SETTINGS.md`, and the overlay controls if it has a key.
4. **Exports:** `src/proxy.def` / `tools/check_exports.py` when a symbol the emulator tests call changes.
5. **Tests:** a native test in `tests/` and an emulator comparison against the stock bytes in `tools/tests/`.
6. **Docs:** `docs/ARCHITECTURE.md`, and `docs/release/CHANGELOG.md` for user-visible changes.

Generated headers (`src/generated/`, `src/core/*.generated.h`) come from `tools/gen_*.py`. Regenerate
them; do not edit them by hand.

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

- **New hooks use the generic mid-hook** (`src/hooks/midhook.h`) by default. Write a naked asm stub
  only with a stated reason (hot path, odd stack), and give that reason in the stub's header comment.
- **One stub file per area** (`src/hooks/stubs_<area>.cpp`). Every stub starts with a 1-3 line header:
  what the game does, what we change, and why.
- **No source file over about 800 lines.** Repeated stub shapes come from a generator or a macro
  (`src/hooks/stub_macros.h`), not from copy-paste.
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
| `src/core/` | Features (neck, tuner, chords, headstock, colour, settings, ...). |
| `src/hooks/` | Stubs, mid-hooks, site registry. |
| `src/d3d/`, `src/overlay/` | Device hooks and the F9 overlay. |
| `sites.json`, `src/generated/` | Patch sites and the tables built from them. |
| `tests/` | Native unit tests. |
| `tools/`, `tools/tests/` | Generators, checks, emulator and site tests. |
| `data/` | Headstock build packages and chord-diagram recipes (MIT, no Ubisoft data). |
| `docs/release/` | User-facing guides, changelog, RS7X format, release process. |
| `docs/DEVELOPMENT.md`, `docs/ARCHITECTURE.md` | Developer reference. |

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
