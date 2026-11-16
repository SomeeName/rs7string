"""Run: uv run pytest tools/tests/test_extblock_lock.py -q (MSVC Win32)."""

from native_harness import ROOT, compile_harness, run_harness


def test_extblock_allocation_failure_does_not_hold_writer_lock(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_lock.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
