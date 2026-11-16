"""Truncation, boundary-value and fixed-seed random malformed RS7X blocks against the real parser and XB1 entry."""

from native_harness import ROOT, compile_harness, run_harness


def test_extblock_malformed_inputs_fail_closed_without_overreads(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_fuzz.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
