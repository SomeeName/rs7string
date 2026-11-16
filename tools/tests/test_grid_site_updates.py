"""Both grid generators append missing sites without depending on JSON whitespace."""

import json

import gen_fretgrid
import gen_nextgrid
import pytest


@pytest.mark.parametrize("generator,sid", [(gen_fretgrid, "FGR123456"), (gen_nextgrid, "NGB123456")])
@pytest.mark.parametrize(
    "text",
    [
        '{"sites":[{"id":"SID","note":"keep"}],"tail":[1,2]}\n',
        '{\n  "sites": [\n    {"id" : "SID", "note": "keep"}\n  ]\n}\n',
    ],
)
def test_grid_write_preserves_existing_sites_and_is_idempotent(tmp_path, monkeypatch, generator, sid, text):
    path, inc = tmp_path / "sites.json", tmp_path / "sites.generated.inc"
    original = text.replace("SID", sid)
    path.write_text(original, encoding="utf-8")
    sites = [{"id": sid, "note": "would overwrite"}, {"id": "NEW", "note": "new"}]
    monkeypatch.setattr(generator, "SITES", path)
    monkeypatch.setattr(generator, "INC", inc)
    monkeypatch.setattr(generator, "build_sites", lambda: sites)
    monkeypatch.setattr(generator, "build_stub_sites", lambda: [])
    assert generator.main(["--write"]) == 0
    first = path.read_bytes()
    doc = json.loads(first)
    assert doc["sites"] == [{"id": sid, "note": "keep"}, {"id": "NEW", "note": "new"}]
    assert doc.get("tail", [1, 2]) == [1, 2]
    assert generator.main(["--write"]) == 0
    assert path.read_bytes() == first
    assert inc.read_text(encoding="utf-8") == generator.render_inc(sites)


def test_append_to_empty_site_array(tmp_path):
    import sitetable

    path = tmp_path / "sites.json"
    path.write_text('{"sites": [], "other": []}\n', encoding="utf-8")
    assert sitetable.append_missing(path, [{"id": "NEW"}]) == 1
    assert json.loads(path.read_text()) == {"sites": [{"id": "NEW"}], "other": []}
