# Contributing to RS Extended Range

**How to build, test and send changes.**

**Contributing** · [Development](docs/DEVELOPMENT.md) · [Architecture](docs/ARCHITECTURE.md) · [Extension block](docs/architecture/extension-block.md) · [Chart tools](docs/release/CHART-TOOLS.md) · [Releasing](docs/RELEASING.md) · [Agents](AGENTS.md)

Thanks for helping. Bug reports, chart tests, docs and code are all welcome.

## Contents

- [Ground rules](#ground-rules)
- [Build and gates](#build-and-gates)
- [Branches and pull requests](#branches-and-pull-requests)
- [Commit style](#commit-style)
- [Testing in game](#testing-in-game)
- [Where the docs live](#where-the-docs-live)

## Ground rules

The rules are in [AGENTS.md](AGENTS.md#rules). They ban Ubisoft data and generated game files and set the branch policy. They apply to everyone, not only to AI agents. Never commit directly to `main`.

## Build and gates

You need Windows, Visual Studio Build Tools (toolset v145, Win32), PowerShell, Python and
[uv](https://docs.astral.sh/uv/). Before you open a pull request, run these gates (checks that must pass):

1. `.\build.ps1`
2. `uv run pytest tools\tests -q`
3. `uv run python tools\sitecheck.py`
4. `uv run python tools\check_exports.py bin\Release\dinput8.dll`

[DEVELOPMENT.md](docs/DEVELOPMENT.md#build-and-test) explains what each step does and lists the flags. It also
covers formatting (`.\scripts\format.ps1`). `sitecheck` and the emulator tests need your own game image. If you
do not have one, skip those gates and say so in the pull request. The build never copies anything into your
game folder.

For docs-only changes, check that every relative link resolves.

## Branches and pull requests

- Make a branch (for example `fix/tuner-arrows`, `feat/...` or `docs/readme`), commit there, and open a
  pull request.
- Keep a pull request small and about one thing.
- Say which gates you ran, and which you skipped and why.

## Commit style

Write a short subject in the imperative mood, with an area prefix:

```text
fix(freeze): never block in GetThreadContext under WoW64
chords: keep the chord frame on the floor
docs: explain the F9 overlay
```

If the reason for the change is not obvious, explain it in the body.

## Testing in game

The rules are in [In-game testing](docs/agents/docs-and-testing.md#in-game-testing). Use your own game copy and plain PSARC charts, don't share anything taken from the game, and put log lines in bug reports. For charts, see the [charting guide](docs/release/CHARTING.md).

## Where the docs live

| Document | For | Contents |
|---|---|---|
| [README](README.md) | Players | Overview, quick install, FAQ. |
| [docs/release/](docs/release/README.md) | Players, chart authors | Install, settings, charting, changelog. |
| [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md) | New developers | How the mod works in five minutes, and where to go next. |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Developers | Build, tools, generated files, debug settings, overlay commands. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Developers | How the mod works, design decisions, known limits. |
| [docs/RELEASING.md](docs/RELEASING.md) | Maintainer | Preparing and publishing a release. |
| [AGENTS.md](AGENTS.md) | AI coding agents | The same rules in short form (Claude Code, Codex and others). Point your AI assistant there. |

By contributing you agree that your work is released under the [MIT License](LICENSE). Please follow
the [Code of Conduct](CODE_OF_CONDUCT.md).
