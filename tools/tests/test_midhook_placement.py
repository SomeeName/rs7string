"""Call placement comes from the same registration used by the DLL."""

import emu_harness
import emu_common


def test_call_after_includes_arrangement_refresh_and_rebuild():
    assert {"ALTF", "ALTR"} <= emu_harness.CALL_AFTER
    assert not {"ALTL", "ALTS"} & emu_harness.CALL_AFTER


def test_call_after_reads_chained_modifiers_and_ignores_comments(tmp_path, monkeypatch):
    feature = tmp_path / "src/features/example"
    feature.mkdir(parents=True)
    (feature / "registration.cpp").write_text(
        'Rs7_Mid("A", &callback).Gate(kGate).After().Light(),\nRs7_Mid("B", &callback), // .After()\n',
        encoding="utf-8",
    )
    monkeypatch.setattr(emu_common, "REPO", tmp_path)
    assert emu_common.call_after() == {"A"}
