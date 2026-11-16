"""The native runner retries refused setup and preserves every correctness failure."""

import pytest

from native_harness import ROOT, compile_harness, run_harness


@pytest.fixture(scope="module")
def runner(tmp_path_factory):
    return compile_harness(ROOT / "tools/tests/native/test_runner.cpp", tmp_path_factory.mktemp("test_runner"))


@pytest.mark.parametrize(
    "mode,returncode,attempts",
    [("setup", 0, 2), ("correctness", 1, 1), ("exception", 1, 1), ("mixed", 1, 1), ("exhausted", 1, 3)],
)
def test_retries_only_setup_without_correctness_failures(runner, mode, returncode, attempts):
    result = run_harness(runner, mode)
    assert result.returncode == returncode, result.stdout + result.stderr
    assert f"attempts={attempts}" in result.stdout, result.stdout
