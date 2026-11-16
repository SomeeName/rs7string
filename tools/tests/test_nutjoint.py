"""The 7th string meets its headstock segment at the nut like strings 0-5."""

from native_harness import ROOT, compile_harness, run_harness


def test_extrapolated_attach_meets_string_size_7_nut_end(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nutjoint.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.splitlines() == ["ok"], result.stdout


def test_segment_7_node_is_not_sheared():
    # The old workaround wrote a shear into string_size_7's local rotate (+4Ch) / translate (+70h), 0.30 at
    # the nut end: more than the end radius (0.20), the visible gap at the nut. The model's node stays as is.
    source = "".join(p.read_text() for p in sorted((ROOT / "src/core").glob("assetload_*.cpp")))
    assert "kSeg7NutDrop" not in source
    assert "h.seg7 + 0x4C" not in source and "h.seg7 + 0x70" not in source
