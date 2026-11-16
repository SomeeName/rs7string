"""Manifest JSON key readers (model/manifest.h JsonNumber, HasOffset) against bad pointers and random doubles."""

from native_harness import ROOT, compile_harness, run_harness


def test_json_key_readers_fail_closed(tmp_path):
    harness = tmp_path / "jsonkey_fuzz.cpp"
    harness.write_text((ROOT / "tools/tests/native/jsonkey_fuzz.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
