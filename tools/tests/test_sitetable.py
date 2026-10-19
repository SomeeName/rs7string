"""sites.json parser and generated header: pattern tokens, kinds (rel32, call, ref, callref, bytes), rejected input."""

import json

import pytest

import gen_sites
import sitetable


def write(tmp_path, sites):
    p = tmp_path / "sites.json"
    p.write_text(json.dumps({"pe_checksum": "0x00B13D7C", "sites": sites}), encoding="utf-8")
    return p


def test_parse_pattern_wildcards():
    assert sitetable.parse_pattern("8B ?? 0c") == [0x8B, None, 0x0C]


def test_parse_pattern_rejects_bad_token():
    with pytest.raises(sitetable.SiteError):
        sitetable.parse_pattern("8B XY")


def test_rel32_and_call_derive_their_target_fields(tmp_path):
    p = write(
        tmp_path,
        [
            {"id": "A", "kind": "rel32", "pattern": "0F 87", "offset": 2, "expect": "84 00 00 00"},
            {"id": "B", "kind": "call", "pattern": "E8", "offset": 0, "expect": "E8 01 02 03 04"},
        ],
    )
    checksum, sites = sitetable.load(p)
    assert checksum == 0x00B13D7C
    assert (sites[0].length, sites[0].target) == (4, (0, 4, 4))
    assert (sites[1].length, sites[1].target) == (5, (1, 4, 5))


def test_rejects_unknown_kind_duplicate_and_short_expect(tmp_path):
    with pytest.raises(sitetable.SiteError):
        sitetable.load(write(tmp_path, [{"id": "A", "kind": "nop", "pattern": "90", "offset": 0, "expect": "90"}]))
    with pytest.raises(sitetable.SiteError):
        sitetable.load(
            write(
                tmp_path,
                [
                    {"id": "A", "kind": "rel32", "pattern": "90", "offset": 0, "expect": "00 00 00 00"},
                    {"id": "A", "kind": "rel32", "pattern": "90", "offset": 0, "expect": "00 00 00 00"},
                ],
            )
        )
    with pytest.raises(sitetable.SiteError):
        sitetable.load(
            write(
                tmp_path,
                [{"id": "A", "kind": "jmp", "length": 6, "pattern": "90", "offset": 0, "expect": "90 90 90 90 90"}],
            )
        )


def test_generated_header_lists_every_site(tmp_path):
    p = write(
        tmp_path,
        [
            {
                "id": "S2",
                "kind": "jmp",
                "length": 5,
                "pattern": "83 FA 05 77 57",
                "offset": 0,
                "expect": "83 FA 05 77 57",
                "target": {"rel_at": 4, "rel_size": 1, "insn_end": 5},
            }
        ],
    )
    text = gen_sites.render(*sitetable.load(p))
    assert "kRs7PeChecksum = 0x00B13D7Cu" in text
    assert '{"S2", "83 FA 05 77 57", 0, "83 FA 05 77 57", Rs7Kind::Jmp, 5, 4, 1, 5, ""},' in text
    assert "kRs7SiteCount = 1" in text


def test_callref_kind_is_a_never_written_call(tmp_path):
    p = write(
        tmp_path,
        [{"id": "X", "kind": "callref", "pattern": "E8 ?? ?? ?? ?? C3", "offset": 0, "expect": "E8 01 02 03 04"}],
    )
    _, sites = sitetable.load(p)
    assert (sites[0].length, sites[0].target) == (5, (1, 4, 5))
    assert "Rs7Kind::CallRef, 5, 1, 4, 5" in gen_sites.render(*sitetable.load(p))
    with pytest.raises(sitetable.SiteError):
        sitetable.load(
            write(tmp_path, [{"id": "X", "kind": "callref", "pattern": "90", "offset": 0, "expect": "90 01 02 03 04"}])
        )


def test_ref_kind_is_four_bytes_and_has_no_branch_target(tmp_path):
    p = write(
        tmp_path, [{"id": "X", "kind": "ref", "pattern": "A1 ?? ?? ?? ??", "offset": "0x1", "expect": "00 0B 36 01"}]
    )
    _, sites = sitetable.load(p)
    assert sites[0].kind == "ref" and sites[0].length == 4 and sites[0].target is None


def test_ref_kind_needs_four_expect_bytes(tmp_path):
    p = write(
        tmp_path, [{"id": "X", "kind": "ref", "pattern": "A1 ?? ?? ?? ??", "offset": "0x1", "expect": "00 0B 36"}]
    )
    with pytest.raises(sitetable.SiteError):
        sitetable.load(p)


def test_ref_kind_rejects_a_branch_target(tmp_path):
    p = write(
        tmp_path,
        [
            {
                "id": "X",
                "kind": "ref",
                "pattern": "A1 ?? ?? ?? ??",
                "offset": "0x1",
                "expect": "00 0B 36 01",
                "target": {"rel_at": 0, "rel_size": 1, "insn_end": 1},
            }
        ],
    )
    with pytest.raises(sitetable.SiteError):
        sitetable.load(p)


def test_generated_header_maps_ref_kind(tmp_path):
    p = write(
        tmp_path, [{"id": "X", "kind": "ref", "pattern": "A1 ?? ?? ?? ??", "offset": "0x1", "expect": "00 0B 36 01"}]
    )
    text = gen_sites.render(*sitetable.load(p))
    assert '{"X", "A1 ?? ?? ?? ??", 1, "00 0B 36 01", Rs7Kind::Ref, 4, -1, 0, 0, ""},' in text


def test_bytes_kind_writes_same_length_bytes(tmp_path):
    p = write(
        tmp_path,
        [{"id": "W", "kind": "bytes", "pattern": "80 FA 06 72 C8", "offset": "0x2", "expect": "06", "write": "07"}],
    )
    _, sites = sitetable.load(p)
    assert (sites[0].kind, sites[0].length, sites[0].target, sites[0].write) == ("bytes", 1, None, b"")
    assert '{"W", "80 FA 06 72 C8", 2, "06", Rs7Kind::Bytes, 1, -1, 0, 0, "07"},' in gen_sites.render(
        *sitetable.load(p)
    )
    for bad in ({"write": "07 07"}, {"write": "06"}, {}):
        e = {"id": "W", "kind": "bytes", "pattern": "80 FA 06", "offset": "0x2", "expect": "06", **bad}
        with pytest.raises(sitetable.SiteError):
            sitetable.load(write(tmp_path, [e]))
