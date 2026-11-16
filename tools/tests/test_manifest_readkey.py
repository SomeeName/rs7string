"""Exercise the exact ReadKey function with synthetic Win32 JSON vtables."""

from native_harness import ROOT, compile_harness, run_harness


def test_manifest_numeric_reader_boundaries_and_bad_nodes(tmp_path):
    manifest = (ROOT / "src/model/manifest.h").read_text()
    source = (ROOT / "src/features/tuner/hooks.cpp").read_text()
    start = source.index("// ---- the manifest key reader:")
    end = source.index("// ---- end of the manifest key reader", start)
    (tmp_path / "readkey.inc").write_text(manifest + "\n" + source[start:end])
    harness = tmp_path / "manifest_readkey.cpp"
    harness.write_text((ROOT / "tools/tests/native/manifest_readkey.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
