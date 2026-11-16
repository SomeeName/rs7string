"""Bass setup must publish its state even if diagnostic logging throws."""

from native_harness import ROOT, compile_harness, run_harness


def test_bass_setup_survives_logging_allocation_failure(tmp_path):
    sources = [ROOT / "src/features/bass" / name for name in ("bass.cpp", "hooks.cpp")]
    (tmp_path / "bass_setup.inc").write_text("\n".join(source.read_text() for source in sources))
    harness = tmp_path / "bass_setup.cpp"
    harness.write_text((ROOT / "tools/tests/native/bass_setup.cpp").read_text())
    result = run_harness(compile_harness(harness, tmp_path))
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
