"""Deterministic truncation/bit-mutation coverage of the real RS7X parser."""

from native_harness import ROOT, compile_harness, run_harness


def test_extblock_mutations_truncations_and_null_input(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_mutations.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
