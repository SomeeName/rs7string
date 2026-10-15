import hashlib
import pytest
import gen_headstock as hs
from pathlib import Path


def test_delta_roundtrip_and_no_inherited_literals():
    templates = [b"old structural template bytes" * 3, b"peg template bytes" * 3]
    target = templates[0][:23] + b"OUR NEW GEOMETRY" + templates[1][4:35]
    ops = hs.delta(target, templates)
    assert hs.apply(ops, templates) == target
    assert any(op[0] == 1 for op in ops)
    for op in ops:
        if op[0] == 0:
            hs.audit_runs(op[1], templates)


def test_audit_rejects_template_run():
    with pytest.raises(ValueError, match="template run"):
        hs.audit_runs(b"prefix" + bytes(range(32)) + b"suffix", [bytes(range(32))])


def test_package_roundtrip():
    templates = [bytes(range(100)), bytes(range(100, 200))]
    out = b"our geometry" + templates[0][3:58]
    package = hs.package(templates, [("rs7_inline7.nif", out, hs.delta(out, templates))])
    hs.audit_runs(package, templates)
    assert hs.unpack(package, templates) == {"rs7_inline7.nif": out}
    with pytest.raises(ValueError, match="template hash"):
        hs.unpack(package, [b"wrong", templates[1]])


def test_known_sha():
    assert hashlib.sha256(b"abc").hexdigest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"


def test_shipped_package_owner_identity_and_no_template_runs():
    names = ("epi_wilshire_cherry.nif", "epi_whilshire_peg.nif")
    if not all((hs.TEMPLATES / n).exists() for n in names):
        pytest.skip("owner templates absent")
    templates = [(hs.TEMPLATES / n).read_bytes() for n in names]
    package = (Path(hs.DEST) / "rs7string_headstock.bin").read_bytes()
    hs.audit_runs(package, templates)
    outputs = hs.unpack(package, templates)
    assert len(outputs) == 9
    for name, data in outputs.items():
        assert data == (hs.OUTPUTS / name).read_bytes()


def test_shipped_lefty_package_owner_identity_and_no_template_runs():
    package = (Path(hs.DEST) / "rs7string_headstock_lh.bin").read_bytes()
    if not all((hs.TEMPLATES_LH / n).exists() for n in hs.LH_NAMES):
        pytest.skip("owner templates absent")
    templates = [(hs.TEMPLATES_LH / n).read_bytes() for n in hs.LH_NAMES]
    hs.audit_runs(package, templates)
    outputs = hs.unpack(package, templates)
    assert list(outputs) == ["rs7_inline7_lh.nif"]
    assert outputs["rs7_inline7_lh.nif"] == (hs.OUTPUTS / "rs7_inline7_lh.nif").read_bytes()
    # the right-handed frame is refused: the package is bound to the left-handed template hashes
    rh = (hs.TEMPLATES / "epi_wilshire_cherry.nif").read_bytes()
    with pytest.raises(ValueError, match="template hash"):
        hs.unpack(package, [rh, templates[1]])
