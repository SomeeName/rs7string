# Preparing a release

**How the maintainer prepares and publishes an RS Extended Range release.**

[Contributing](../CONTRIBUTING.md) · [Development](DEVELOPMENT.md) · [Architecture](ARCHITECTURE.md) · [Extension block](architecture/extension-block.md) · [Chart tools](release/CHART-TOOLS.md) · **Releasing** · [Agents](../AGENTS.md)

> [!IMPORTANT]
> The static preparation on this page does not prove that the release is ready in the game. Before you
> publish, complete the maintainer's release acceptance process separately. Its manual in-game part is the
> [test checklist](TEST-CHECKLIST.md).

## Contents

- [Before you start](#before-you-start)
- [Dry run](#dry-run)
- [Gates](#gates)
- [Outputs](#outputs)
- [Publishing explicitly](#publishing-explicitly)

## Before you start

| Requirement | Notes |
|---|---|
| Tools | Windows, Visual Studio Build Tools (v145, Win32), PowerShell, Git and `uv` (the Python dependencies come from `pyproject.toml`). |
| Clean checkout | Commit the release changes and the changelog first. Untracked files also make the checkout dirty. Ignored build outputs and `dist/` are allowed. |
| Changelog | `-Version` must match a `##` heading in [CHANGELOG.md](release/CHANGELOG.md) exactly, for example `1.0.0-beta.4`. Bracketed headings and a ` - date` suffix are also accepted. Missing, duplicate or empty sections fail before building. |
| Tag | The tag is `v<version>`. An existing local tag fails before building. If the checkout may be missing remote tags, fetch the tags first. |

## Dry run

```powershell
.\scripts\release.ps1 -Version 1.0.0-beta.4 -DryRun
```

- Omitting `-DryRun` has the same effect.
- A dry run runs all static gates and writes the artifacts.
- It never tags, pushes, publishes, launches the game or accesses the game folder.
- It does not need `gh` or network access.

> [!TIP]
> On Windows, Unicorn's handled first-chance exceptions can flood pytest's faulthandler output. If that
> happens, turn off only that reporting, as `tools/tests/emu_harness.py` describes. All tests still
> run:
>
> ```powershell
> $env:PYTEST_ADDOPTS = '-p no:faulthandler'
> .\scripts\release.ps1 -Version 1.0.0-beta.4 -DryRun
> ```

## Gates

The script runs these in order and stops at the first failure:

1. `build.ps1`, including the C++ unit tests, with `/p:PostBuildEventUseInBuild=false` and
   `/p:PlatformToolset=v145`.
2. `uv run pytest tools\tests -q`.
3. `uv run python tools\sitecheck.py`. It needs a local analysis copy of your own game executable (see
   [DEVELOPMENT.md](DEVELOPMENT.md#build-and-test)).
4. `uv run python tools\check_exports.py bin\Release\dinput8.dll`.
5. `scripts\package.ps1`. It builds again, refuses game/Ubisoft files and checks the archive
   against an allowlist.

> [!WARNING]
> Do not bypass the package script's refusal and allowlist checks.

## Outputs

All outputs are ignored by Git:

| File | Contents |
|---|---|
| `dist/rsxr-<version>.zip` | The release zip. `scripts\package.ps1` writes it as `rsxr-<commit>.zip`, and the release script renames it to the version. |
| `dist/SHA256SUMS.txt` | SHA-256 hashes for the zip and the built DLL. Paths are relative to `dist/`, so the DLL entry is `../bin/Release/dinput8.dll`. To verify that entry after downloading only the zip, extract its `bin/Release` tree next to `dist/`. |
| `dist/release-notes-<version>.md` | Only the requested changelog section, including its heading and any subheadings. |

At the end, the script prints the exact PowerShell commands for publishing. Review them and the artifacts.
Both commands pin the current full commit hash. The GitHub command uses the extracted notes as the release body and
attaches the zip, checksums and notes.

## Publishing explicitly

```powershell
.\scripts\release.ps1 -Version 1.0.0-beta.4 -Publish
```

You cannot combine `-Publish` and `-DryRun`. Publishing does these steps:

1. Reruns the preparation.
2. Checks that the tag is absent on `origin` (and fails if the remote cannot be checked).
3. Checks `gh` authentication.
4. Creates a local annotated tag.
5. Runs `gh release create`. GitHub creates the remote tag at the specified commit. There is no
   `git push` step, and no branch is pushed.

Requirements and failure handling:

- The commit must already exist on GitHub, and `origin` and `gh` must refer to the intended repository.
- If GitHub publication fails after tagging, the script deletes the local tag again. Inspect the failure and the
  local/remote state before retrying.
- The script refuses existing tags and does not overwrite an existing release.

### The public tree

The public repository gets one commit per release. Its tree has every tracked file of the private repository,
except the maintainer's private-only files (internal plans, handover notes and the tooling that lists them).

The links in the docs, `mkdocs.yml` and the `pages` workflow already use the public repository's new name,
`SomeeName/rs-extended-range`. The public repository is named `SomeeName/rs7string` until then. Rename it before
the first release from this tree. Otherwise the docs site is not built and its links are dead.
