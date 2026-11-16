# Contributing to rs7string

**How to build, test and send changes.**

**Contributing** · [Development](docs/DEVELOPMENT.md) · [Architecture](docs/ARCHITECTURE.md) · [RS7X spec](docs/release/RS7X-SPEC.md) · [Chart tools](docs/release/CHART-TOOLS.md) · [Releasing](docs/RELEASING.md) · [Agents](AGENTS.md)

Thanks for helping. Bug reports, chart tests, docs and code are all welcome.

## Contents

- [Ground rules](#ground-rules)
- [Build and gates](#build-and-gates)
- [Branches and pull requests](#branches-and-pull-requests)
- [Commit style](#commit-style)
- [Testing in game](#testing-in-game)
- [Where the docs live](#where-the-docs-live)

## Ground rules

The rules (no Ubisoft data, no generated game files, branch policy) are in [AGENTS.md](AGENTS.md#rules) and apply to everyone, not only to AI agents. Never commit directly to `main`.

## Build and gates

You need Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell, Python and
[uv](https://docs.astral.sh/uv/). Run `.\build.ps1`, then `uv run pytest tools\tests -q`,
`uv run python tools\sitecheck.py` and `uv run python tools\check_exports.py bin\Release\dinput8.dll`
before you open a pull request. What each step does, the flags, formatting
(`.\scripts\format.ps1`) and the game-image requirement for `sitecheck` and the emulator tests are in
[DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test). Without your own game image, skip those gates and say so
in the pull request. The build never copies anything into your game folder.

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

The rules (own game copy, plain PSARC charts, nothing game-derived shared, log lines in bug reports) are in [In-game testing](docs/agents/docs-and-testing.md#in-game-testing). For charts see the [charting guide](docs/release/CHARTING.md).

## Where the docs live

| Document | For | Contents |
|---|---|---|
| [README](README.md) | Players | Overview, quick install, FAQ. |
| [docs/release/](docs/release/README.md) | Players, chart authors | Install, settings, charting, changelog, RS7X format. |
| [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md) | New developers | How the mod works in five minutes, and where to go next. |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Developers | Build, tools, generated files, debug settings, overlay commands. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Developers | How the mod works, design decisions, known limits. |
| [docs/RELEASING.md](docs/RELEASING.md) | Maintainer | Preparing and publishing a release. |
| [AGENTS.md](AGENTS.md) | AI coding agents | The same rules in short form (Claude Code, Codex and others). Point your AI assistant there. |

By contributing you agree that your work is released under the [MIT License](LICENSE). Please follow
the [Code of Conduct](CODE_OF_CONDUCT.md).
