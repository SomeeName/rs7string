"""A rejected second load or post-process must remove previously published data."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


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
    ):
        result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=10)
        if result.returncode:
            failures.append(f"{mode}: {result.stdout}{result.stderr}")
    assert not failures, "\n".join(failures)
