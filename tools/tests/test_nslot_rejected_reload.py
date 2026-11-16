"""A rejected second load or post-process must remove previously published data."""

from native_harness import ROOT, compile_harness, run_harness


def test_nslot_rejected_reload_unpublishes_arrays_and_allows_retry(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_rejected_reload.cpp", tmp_path)
    failures = []
    for mode in (
        "reload",
        "partial",
        "template-copy",
        "record-copy",
        "template-allocation",
        "scratch-allocation",
        "c8-allocation",
        "t8-allocation",
        "load-scratch-allocation",
        "user-allocation",
        "registry-full",
    ):
        result = run_harness(exe, mode)
        if result.returncode or "PASS" not in result.stdout:
            failures.append(f"{mode}: {result.stdout}{result.stderr}")
    assert not failures, "\n".join(failures)
