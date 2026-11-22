# Agent and contributor guide

**Rules and commands for AI coding agents (and humans) working on RS Extended Range.**

[Contributing](CONTRIBUTING.md) · [Development](docs/DEVELOPMENT.md) · [Architecture](docs/ARCHITECTURE.md) · [Extension block](docs/architecture/extension-block.md) · [Chart tools](docs/release/CHART-TOOLS.md) · [Releasing](docs/RELEASING.md) · **Agents**

RS Extended Range is a `dinput8.dll` mod that adds seven- and eight-string guitar and five- and six-string bass support to Rocksmith 2014
Remastered (Steam, September 2022 build, 32-bit Windows). It patches the running game at byte-pattern
sites (`sites.json`) with naked stubs and mid-hooks. It ships no Ubisoft data.

Read these first: [README.md](README.md), [CONTRIBUTING.md](CONTRIBUTING.md),
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (build, tools, debug settings) and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (how the mod works, decisions, known limits).

## Rules

> [!IMPORTANT]
> - **Work only in `SomeeName/rs-extended-range-dev`.** The public repository (`SomeeName/rs-extended-range`, named `SomeeName/rs7string` until the release) is read-only. Never push or make changes there.
> - **Never commit to `main`.** Work on a branch (`fix/...`, `feat/...`, `docs/...`) and open a pull request.
> - Commit messages and author/co-author fields must not contain AI assistant names or attribution.
> - **Do not push, tag or publish a release** unless the maintainer asked for it.
> - **No Ubisoft data:** no encryption keys, game assets (archives, models, textures, movies, sounds),
>   decrypted content, or disassembly/decompiler listings of the game in the repository, issues or
>   pull requests. Short identifiers, addresses, function names and a few bytes of a pattern are fine.
> - **No generated files** from a user's game install: built `.nif` models, asset dumps, patched movies,
>   memory dumps, game logs with personal paths. `.gitignore` excludes the usual output folders.
> - **Never modify a game folder** from a build or test. Back up a file before you replace it there, and never while
>   the game runs.

- Tag findings in developer docs as CONFIRMED (with the address or log line that shows it) or HYPOTHESIS. Player
  docs use neither tag.

## Build and gates

You need Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell and [uv](https://docs.astral.sh/uv/).
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test) is the main source for the commands, the flags
(`-Asan`, `-Fuzz`, `-SkipAnalysis`), CI and the game-image requirement. Here is the short form:

```powershell
.\build.ps1                                                   # format check, DLL, unit tests, /analyze
uv run pytest tools\tests -q                                  # site table + emulator tests
uv run python tools\sitecheck.py                              # every site resolves in the game image
uv run python tools\check_exports.py bin\Release\dinput8.dll  # exports, image base, imports
```

- A new compiler warning (`/W4 /WX`), a new `/analyze` finding or growth of the layout baseline fails the
  build. Fix the cause, do not disable the check. If you run MSBuild yourself, pass `/p:PlatformToolset=v145` and
  `/p:PostBuildEventUseInBuild=false`.
- `build.ps1` fails on unformatted sources, so run `.\scripts\format.ps1` first.
- `sitecheck.py` and the emulator tests need your own unpacked game executable. Without it, the emulator tests
  are skipped and `sitecheck.py` cannot run. Say so in the pull request. Never commit the executable.
- For docs-only changes, check that every relative link resolves. `mkdocs build --strict` does this (see
  [docs/agents/docs-and-testing.md](docs/agents/docs-and-testing.md)).

The key code rules:

- `tools/gen_*.py` regenerates the generated headers (`src/generated/`, `*.generated.h`). Never edit them by hand.
- Write new hooks as C++ mid-hooks, not as new `__asm` stubs.
- Do not use `gs`, `out` or `in` as operand names in inline asm.
- Keep a feature's sites, hooks, settings and tests in `src/features/<name>/`.
- Test only on your own game copy, and never post output taken from the game.

## Where to read more

| When you... | Read |
|---|---|
| add or remove a hook site, or write inline asm / a naked stub | [docs/agents/hook-sites.md](docs/agents/hook-sites.md) |
| add or move code, a feature, a hook callback (mid-hook modes), or touch formatting, blank-line, size or include rules | [docs/agents/code-structure.md](docs/agents/code-structure.md) (also the full path table) |
| edit player docs (`README.md`, `docs/release/`) or test in the game | [docs/agents/docs-and-testing.md](docs/agents/docs-and-testing.md) |
| need the build, tools, debug settings, CI | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |
| need how the mod works, decisions, limits | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
