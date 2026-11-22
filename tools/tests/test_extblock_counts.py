"""Malformed extension block counts exercised against the real Win32 parser."""

from native_harness import ROOT, compile_harness, run_harness


def test_extblock_rejects_wrapping_counts_before_allocation(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_counts.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
