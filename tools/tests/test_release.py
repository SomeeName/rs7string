"""Release orchestration in disposable repos; never publish or access the game."""

import hashlib
from pathlib import Path
import shutil
import subprocess

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "release.ps1"
SHELL = shutil.which("pwsh") or shutil.which("powershell")
pytestmark = pytest.mark.skipif(not SHELL, reason="needs PowerShell")


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True, text=True).stdout.strip()


@pytest.fixture
def repo(tmp_path):
    root = tmp_path / "release fixture"
    (root / "scripts").mkdir(parents=True)
    shutil.copyfile(SCRIPT, root / "scripts/release.ps1")
    (root / "docs/release").mkdir(parents=True)
    (root / "docs/release/CHANGELOG.md").write_text(
        "# Changes\n\n## 1.0.0\n\nFirst release: café.\n### Fixes\n- A fix.\n\n## 0.9.0\nOld.\n", encoding="utf-8"
    )
    (root / ".gitignore").write_text("bin/\ndist/\n")
    (root / "build.ps1").write_text(
        "New-Item -ItemType Directory -Force bin/Release | Out-Null\n"
        "[IO.File]::WriteAllText((Join-Path $PWD 'bin/Release/dinput8.dll'), 'test DLL')\n"
        "$global:LASTEXITCODE = 0\n"
    )
    (root / "scripts/package.ps1").write_text(
        "New-Item -ItemType Directory -Force dist | Out-Null\n"
        "$hash = (git rev-parse --short HEAD).Trim()\n"
        "Compress-Archive bin/Release/dinput8.dll ('dist/rs7string-' + $hash + '.zip') -Force\n"
        "$global:LASTEXITCODE = 0\n"
    )
    # Substitute the expensive Python gates only; Git, notes, hashes and packaging are real.
    (root / "runner.ps1").write_text(
        "$ErrorActionPreference = 'Stop'\n"
        "function uv { if ($env:RS7_TEST_GATE_FAIL) { $global:LASTEXITCODE = 9 } "
        "else { $global:LASTEXITCODE = 0 } }\n"
        "& ./scripts/release.ps1 @args\n"
    )
    git(root, "init")
    git(root, "add", ".")
    git(root, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-m", "fixture")
    return root


def run(root, *args, env=None):
    return subprocess.run(
        [SHELL, "-NoProfile", "-File", "runner.ps1", "-Version", "1.0.0", *args],
        cwd=root,
        capture_output=True,
        text=True,
        env=env,
        timeout=60,
    )


@pytest.mark.parametrize("args", [(), ("-DryRun",)])
def test_dry_run_prepares_artifacts_without_creating_tag(repo, args):
    result = run(repo, *args)
    assert result.returncode == 0, result.stdout + result.stderr
    assert git(repo, "tag") == ""
    assert "git tag" in result.stdout and "gh release create" in result.stdout
    notes = repo / "dist/release-notes-1.0.0.md"
    assert notes.read_text(encoding="utf-8-sig").strip() == ("## 1.0.0\n\nFirst release: café.\n### Fixes\n- A fix.")
    for line in (repo / "dist/SHA256SUMS.txt").read_text().splitlines():
        digest, name = line.split("  ")
        assert hashlib.sha256((repo / "dist" / name).read_bytes()).hexdigest() == digest.lower()
    assert len((repo / "dist/SHA256SUMS.txt").read_text().splitlines()) == 2


@pytest.mark.parametrize("reason", ["dirty", "tag", "notes", "gate"])
def test_preflight_and_gate_failures_do_not_prepare_release(repo, reason, monkeypatch):
    if reason == "dirty":
        (repo / "untracked.txt").write_text("dirty")
    elif reason == "tag":
        git(repo, "tag", "v1.0.0")
    elif reason == "notes":
        (repo / "docs/release/CHANGELOG.md").write_text("## 1.0.1\nWrong version.\n")
        git(repo, "add", ".")
        git(repo, "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-m", "other notes")
    else:
        monkeypatch.setenv("RS7_TEST_GATE_FAIL", "1")
    result = run(repo, "-DryRun")
    assert result.returncode != 0
    message = {
        "dirty": "Working tree must be clean",
        "tag": "already exists",
        "notes": "changelog section",
        "gate": "uv failed",
    }[reason]
    assert message in result.stdout + result.stderr
    assert not (repo / "dist/SHA256SUMS.txt").exists()
    assert not list(repo.glob("dist/*.zip"))


def test_publish_and_dry_run_cannot_be_combined(repo):
    result = run(repo, "-DryRun", "-Publish")
    assert result.returncode != 0
    assert git(repo, "tag") == ""
    assert not (repo / "dist").exists()
