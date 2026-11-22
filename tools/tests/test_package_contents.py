"""Release input and ZIP allowlists reject stray chord-diagram files."""

import shutil
import subprocess
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SHELL = shutil.which("pwsh") or shutil.which("powershell")
pytestmark = pytest.mark.skipif(not SHELL, reason="needs PowerShell")
DIAGRAMS = ("commonlib.rsxrda", "contentpanel.rsxrda", "ge_game.rsxrda", "LICENSE", "README.md")


def run_package_functions(tmp_path, command):
    # Load the real functions without running the build or creating a release.
    prefix = (ROOT / "scripts/package.ps1").read_text(encoding="utf-8").split("Push-Location $root\ntry {")[0]
    script = tmp_path / "check.ps1"
    script.write_text(prefix + "\n$root = $PSScriptRoot\n" + command, encoding="utf-8")
    return subprocess.run(
        [SHELL, "-NoProfile", "-File", str(script)],
        capture_output=True,
        text=True,
        timeout=60,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )


def diagram_inputs(tmp_path):
    folder = tmp_path / "data/chord-diagrams"
    folder.mkdir(parents=True)
    for name in DIAGRAMS:
        (folder / name).write_bytes(b"recipe fixture")
    return folder


def test_chord_diagram_inputs_are_exact(tmp_path):
    diagram_inputs(tmp_path)
    result = run_package_functions(tmp_path, "Get-ChordDiagramFiles | ForEach-Object { $_.Name }")
    assert result.returncode == 0, result.stderr
    assert set(result.stdout.splitlines()) == set(DIAGRAMS)


@pytest.mark.parametrize("extra", ["scratch.log", "scratch.rsxrda", "nested/README.md", "nested/commonlib.rsxrda"])
def test_chord_diagram_inputs_reject_unexpected_files(tmp_path, extra):
    folder = diagram_inputs(tmp_path)
    path = folder / extra
    path.parent.mkdir(exist_ok=True)
    path.write_text("private scratch content", encoding="utf-8")
    result = run_package_functions(tmp_path, "Get-ChordDiagramFiles")
    assert result.returncode != 0
    assert "unexpected chord-diagram file" in result.stderr


def test_chord_diagram_inputs_require_all_intended_files(tmp_path):
    folder = diagram_inputs(tmp_path)
    (folder / "commonlib.rsxrda").unlink()
    result = run_package_functions(tmp_path, "Get-ChordDiagramFiles")
    assert result.returncode != 0
    assert "missing chord-diagram file" in result.stderr


@pytest.mark.parametrize("extra", [None, "scratch.log", "scratch.rsxrda", "nested/README.md"])
def test_zip_allowlist_checks_exact_chord_diagram_names(tmp_path, extra):
    path = tmp_path / "release.zip"
    with zipfile.ZipFile(path, "w") as archive:
        for name in (
            "dinput8.dll",
            "rsxr.ini",
            "rsxr_headstock.bin",
            "rsxr_headstock_lh.bin",
            "rsxr_headstock_bass.bin",
            "rsxr_headstock_bass_lh.bin",
        ):
            archive.writestr("bin/Release/" + name, b"fixture")
        archive.writestr("docs/release/INSTALL.md", b"fixture")
        archive.writestr("tools/rsxr-convert.exe", b"fixture")
        for name in DIAGRAMS:
            archive.writestr("bin/Release/chord-diagrams/" + name, b"fixture")
        if extra:
            archive.writestr("bin/Release/chord-diagrams/" + extra, b"scratch")
    result = run_package_functions(tmp_path, "Test-ZipContents (Join-Path $root 'release.zip')")
    assert (result.returncode == 0) == (extra is None), result.stderr
    if extra:
        assert "not in the release allowlist" in result.stderr
