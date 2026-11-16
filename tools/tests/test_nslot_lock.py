"""Native fault injection against the real nslot source; never launches the game."""

from native_harness import ROOT, compile_harness, run_harness


def test_nslot_allocation_failure_does_not_hold_writer_lock(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_lock.cpp", tmp_path)
    failures = []
    for args in ([], ["postprocess"]):
        result = run_harness(exe, *args)
        if result.returncode or "PASS" not in result.stdout:
            failures.append(f"{args}: {result.stdout}{result.stderr}")
    assert not failures, "\n".join(failures)
