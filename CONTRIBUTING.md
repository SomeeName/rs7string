# Contributing to rs7string

**How to build, test and send changes.**

**Contributing** · [Development](docs/DEVELOPMENT.md) · [Architecture](docs/ARCHITECTURE.md) · [RS7X spec](docs/release/RS7X-SPEC.md) · [Releasing](docs/release/RELEASING.md) · [Agents](AGENTS.md)

Thanks for helping. Bug reports, chart tests, docs and code are all welcome.

## Contents

- [Ground rules](#ground-rules)
- [Build](#build)
- [Gates](#gates)
- [Branches and pull requests](#branches-and-pull-requests)
- [Commit style](#commit-style)
- [Testing in game](#testing-in-game)
- [Where the docs live](#where-the-docs-live)

## Ground rules

> [!IMPORTANT]
> - No Ubisoft encryption keys, game assets (archives, models, textures, movies, sounds) or decrypted
>   content in commits, issues or pull requests.
> - No files generated from your game install (built models, dumps, game logs with personal paths).
> - Never commit directly to `main`.

## Build

You need Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell, Python and
[uv](https://docs.astral.sh/uv/).

```powershell
.\build.ps1             # DLL + C++ unit tests
.\build.ps1 -NoTests    # build only
```

- Output: `bin\Release\dinput8.dll` and `bin\Release\rs7string_tests.exe`.
- `build.ps1` passes `/p:PlatformToolset=v145` and `/p:PostBuildEventUseInBuild=false`. If you call
  MSBuild yourself, pass both.
- The build never copies anything into your game folder.

## Gates

Run these before you open a pull request:

```powershell
uv run pytest tools\tests -q
uv run python tools\sitecheck.py
uv run python tools\check_exports.py bin\Release\dinput8.dll
```

| Gate | Checks | Needs |
|---|---|---|
| `pytest` | Site table and emulator tests. | Tests that need the game image skip themselves without it. |
| `sitecheck` | Every patch site resolves in the game image. | A local, unpacked copy of your own game executable ([DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test)). Never commit it. Without it, skip this gate and say so in the pull request. |
| `check_exports` | Exports, image base, and no `d3d9.dll` import. | The built DLL. |

> [!TIP]
> On Windows, if pytest output is flooded by emulator exceptions, set
> `PYTEST_ADDOPTS=-p no:faulthandler`. All tests still run.

Format your changes with `.\scripts\format.ps1` (the build fails on unformatted sources; details in
[DEVELOPMENT.md](docs/DEVELOPMENT.md#formatting)).

For docs-only changes, check that every relative link resolves.

## Branches and pull requests

- Make a branch (for example `fix/tuner-arrows`, `feat/...` or `docs/readme`), commit there, and open a
  pull request.
- Keep a pull request small and about one thing.
- Say which gates you ran, and which you skipped and why.

## Commit style

A short, imperative subject with an area prefix:

```text
fix(freeze): never block in GetThreadContext under WoW64
chords: keep the chord frame on the floor
docs: explain the F9 overlay
```

Explain the why in the body when it is not obvious.

## Testing in game

- Test with plain (unencrypted) PSARC charts made for rs7string. See the
  [charting guide](docs/release/CHARTING.md).
- Never replace files in the game folder while the game runs. Back up any file first.
- Include the relevant `rs7string.log` lines when you report a game-side problem. Remove personal paths.

## Where the docs live

| Document | For | Contents |
|---|---|---|
| [README](README.md) | Players | Overview, quick install, FAQ. |
| [docs/release/](docs/release/README.md) | Players, chart authors | Install, settings, charting, changelog, RS7X format, release process. |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Developers | Build, tools, generated files, debug settings, overlay commands. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Developers | How the mod works, design decisions, known limits. |
| [AGENTS.md](AGENTS.md) | AI coding agents | The same rules in short form (Claude Code, Codex and others). Point your AI assistant there. |

By contributing you agree that your work is released under the [MIT License](LICENSE). Please follow
the [Code of Conduct](CODE_OF_CONDUCT.md).
