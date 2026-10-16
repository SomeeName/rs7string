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


def _fretgrid_ids():
    """Native capacity-8 FretGrid sites (tools/gen_fretgrid.py, issue #18 stage 5): FGR<va> in address order, then FGRB."""
    import gen_fretgrid

    return [x["id"] for x in gen_fretgrid.build_sites() + gen_fretgrid.build_stub_sites()]


def _nextgrid_ids():
    """Native capacity-8 next-note grid sites (tools/gen_nextgrid.py, issue #18 stage 5 area 2): NGB<va> in address order, then NG1 NGP NG9."""
    import gen_nextgrid

    return [x["id"] for x in gen_nextgrid.build_sites() + gen_nextgrid.build_stub_sites()]


def _tuner8_ids():
    """The native tuner session sites in sites.json order (tools/gen_tuner8s.py --sites, then mid-hooks)."""
    return [
        "TS8SEL",
        "TS8STA",
        "TS8STB",
        "TS8M2E",
        "TS8MD2",
        "TS8MD0",
        "TS8MD1",
        "TS8MD3",
        "TS8FIN",
        "TS8FOU",
        "TS8PLS",
        "TS8PEK",
        "TS8LET",
        "TS8LPL",
        "TS8LRL",
        "TS8PKS",
        "TS8PKR",
        "TS8PKP",
        "TS8PKL",
        "TS8LOF",
        "TS8HNT",
        "TS8F1",
        "TS8F2",
        "TS8F3",
        "TS8RS",
        "TS8DT",
        "TS8WR",
        "TS8SN1",
        "TS8SN2",
        "TS8PA",
        "TS8PZ",
    ]


def test_real_sites_json_is_valid():
    from pathlib import Path

    checksum, sites = sitetable.load(Path(__file__).resolve().parents[2] / "sites.json")
    assert checksum == 0x00B13D7C
    assert [s.id for s in sites if not s.id.startswith("CM")] == [
        "S4",
        "S2",
        "S1",
        "S3",
        "R2",
        "R3a",
        "R3b",
        "R3c",
        "R4",
        "R6",
        "R1",
        "D2",
        "D3",
        "D5",
        "X_CM",
        "X_TCS",
        "X_THEAD",
        "X_SPAWN",
        "X_ADV3",
        "N1",
        "N5",
        "X_ALLOC",
        "X_STRCTOR",
        "X_SHAPE",
        "X_SHOW",
        "X_SETPOS",
        "X_SETROT",
        "X_DT",
        "X_WORLD",
        "X_ROOT",
        "N12",
        "N13",
        "N18",
        "N19",
        "N20",
        "N21",
        "X_DETACH",
        "N22",
        "N23",
        "X_SETSCALE",
        "F1",
        "LT1",
        "TF1",
        "IN1",
        "X_EULER",
        # native N-slot PegSet (core/headstock.h, issue #18 stage 3)
        "PSA1",
        "PSA2",
        "PSD1",
        "PSD2",
        "PSU1",
        "PSU2",
        "PSU3",
        "PSM1",
        "PSM2",
        "PSQ1",
        "PSC1",
        "PSL1",
        "PSL2",
        "PSR1",
        "PSN1",
        "X_PEGNAMES",
        "PSN2",
        "X_PEGSELECT",
        "X_PEGPLACE",
        "X_PEGLAYER",
        # the 7-string tuner (core/tuner.h)
        "X_TMGR",
        "X_GETDET",
        "W",
        "X_FADEMGR",
        "X_FADEIN",
        "X_FADEOUT",
        "TK",
        "TS",
        "AT",
        # slide cell row stride (V2) and bend direction of string 6 (issue #14)
        "V2",
        "BD1",
        "BD2",
        "BD3",
        "BD4",
        # edited headstock model (core/assetload.h)
        "X_FIXSTR",
        "AL1",
        "X_STRPARAMS",
        # the 7-string model's textures, the 7th knob's tint (issue #17)
        "X_DDSH",
        "X_NFCTOR",
        "X_NFDTOR",
        # virtual guitarist ([test] vguitar, core/vguit.h)
        "X_VGSLOT",
        "X_VGDI",
        "TICK",
        "JUD1",
        "JUD2",
        "AUD1",
        # M4 chords with a string-6 column (core/chord7.h)
        "B1",
        "X_SST",
        "B2",
        # seven-string tuning names (core/tuningname.h, issue #15)
        "TJ",
        "TU",
        "TUT",
        "TN",
        "X_STRCON",
        "X_STRASG",
        "X_LOC",
        "X_STRDTOR",
        # song end log (core/songend.h, issue #13)
        "SE1",
        "SE2",
        "SE3",
        # RS7X SNG extension block (core/extblock.h, issue #18)
        "XB1",
        # native N-slot chord data (core/nslot.h, issue #18 stage 2)
        "TP1",
        "TP2",
        "TP3",
        "TP4",
        "TP5",
        "TP6",
        "TP7",
        "TP8",
        "TP11",
        "CP1",
        "CP2",
        "PP1",
        "SF1",
        # native N-slot chord rendering (byte sites, TM1-TM6, DS1)
        "NR1",
        "NR2",
        "NR3",
        "NR4",
        "NR5",
        "NR6",
        "NB1",
        "NB2",
        "NS1",
        "NS2",
        "NS3",
        "NS4",
        "NS5",
        "NS6",
        "NS7",
        "NS8",
        "NS9",
        "NS10",
        "NS11",
        "NS12",
        "CT1",
        "CT2",
        "CT3",
        "CT4",
        "CT5",
        "CT6",
        "CT7",
        "CT8",
        "CT9",
        "CT10",
        "CT11",
        "SH1",
        "SH2",
        "SH3",
        "SH4",
        "BH1",
        "HS1",
        "HS2",
        "NN1",
        "NN2",
        "NN3",
        "NN4",
        "NN5",
        "NN6",
        "NK1",
        "TM1",
        "TM2",
        "TM3",
        "TM4",
        "TM5",
        "TM6",
        "NM1",
        "NM2",
        "NM3",
        "NM4",
        "NM5",
        "NM6",
        "NM7",
        "NM8",
        "NM9",
        "NM10",
        "FG1",
        "FG2",
        "FG3",
        "FG4",
        "FG6",
        "FG7",
        "FG8",
        "FG9",
        "DS1",
        "SMa",
        "SMb",
        "SMc",
        "DM1",
        # native N-slot chord detection (core/nsmatch.h, tracker eval tails)
        "MD1",
        "X_MSORT",
        "X_MINS1",
        "X_MINS2",
        "X_MCMP",
        "X_MTAB",
        "RQ1",
        "RQ2",
        "RQ3",
        "RQ4",
        "RQ5",
        "RQ6",
        "PR1",
        "PR2",
        "PR3",
        "MS1",
        "MS2",
        "LU1",
        "LU2",
        "LU3",
        "AM1",
        "WC1",
        "WC2",
        "WC3",
        "WC4",
        "WC5",
        "WC6",
        "WC7",
        "EV1",
        "EV2",
        "EV3",
        "EV4",
        "EV5",
        "BT1",
        "CS1",
        # 7th string pluck in chords
        "TP12",
        "PL1",
        # native N-slot chord tracking (core/nstracker.h)
        "CN1",
        "CN2",
        "CN3",
        "CC1",
        "CC2",
        "CC3",
        "BF1",
        "CH1",
        "CH2",
        "CH3",
        "X_EVREL",
        "RL1",
        "RL2",
        "AG1",
        "AG2",
        "AG3",
        "UP1",
        "UP2",
        "UP3",
        "PD1",
        "PD2",
        "PD3",
        "PD4",
        "PD5",
        "NF1",
        "NF2",
        "NF3",
        "NF4",
        # native N-slot GuitarStrings (core/gsnative.h, issue #18 stage 4)
        "GSFR",
        "GSBD",
        "GSUL",
        "GSLD",
        "GSFO",
        "GSFI",
        "GSXF",
        "GSWD",
        "GSUP",
        "GSRS",
        "GSPK",
        "GSVB",
        "GSIC",
        "GSDM",
        "GSGL",
        "GSHC",
        "GSA1",
        "GSC1",
        "GSZ1",
        "GIA1",
        "GIZ1",
        "GRM1",
        "GRM2",
        "GRM3",
        "GSSS",
        "GSSE",
        "X_ATTACH",
        "I1",
        "DGM",
        "DGS",
        "TP13",
        "DGB",
        "X_DKEY",
        "X_DNUMBER",
        "X_DSET",
        "X_DDROP",
    ] + _fretgrid_ids() + [
        # [neck] native_pitch (core/pitch8.h, issue #18 stage 5 area 3)
        "PA1",
        "PA2",
        "PM0",
        "PR0",
        "PT1",
        "PM1",
        "PE0",
        "PC1",
        "PC2",
        "PC3",
        "PC4",
    ] + _nextgrid_ids() + ["PUBCL", "PUBTO", "PUBWU", "PUBNN1", "PUBNN3", "PUBNN4"] + [
        "PUBGS",
        "PUBGE1",
        "PUBGE2",
        "PUBGC",
        "PUBGO",
        "PUBGB",
        "PUBGA",
        "PUBGF",
        "PUBEDGE",
        "PUBNL",
        "PUBAN",
        "PUBAC",
        "PUBZ0",
        "PUBZ1",
        "PUBZ2",
        "PUBZ3",
        "PUBZ4",
        "PUBZ5",
        "PUBZ6",
    ] + [
        "CLB0",
        "CLBM",
        "CLBF",
        "CLB1",
        "CLBG",
        "CLBA",
        "CLGU",
        "CLGE",
        "CLVB",
        "CLNF2",
        "CLNF3",
        "CLCT4",
        "CLCT5",
        "CLCT6",
        "CLCT7",
        "CLCT8",
        "CLCT9",
        "CLCT10",
        "CLCT11",
    ] + ["TU8H", "TU8C", "TU8A", "TU8ALLOC", "TU8INIT", "TU8FREE"] + _tuner8_ids() + [
        "GKC",
        "GKE",
        "GKM",
        "GKL",
        "GKT",
        "GKH1",
        "GKH2",
    ] + ["LSA", "LSG", "LSC", "LSE"] + ["PP_SNGD", "PP_SNGF", "PP_TOC"]  # plain PSARC (group psarc)
    assert (
        len([s for s in sites if not s.publish]) == 626 + 19 + 6 + 31 + 4 + 3 + 7 + 3 + 4 + 2 - 7 - 26 - 7 - 21 - 2 + 2
    )  # 1.0: - C1-C5 N11 GSCL, - the B-phase tuner (15 sites, 11 refs), - the stage-2 peg (PG1-4 PH1 PH2 X_GIVE), - the S7 object (10 sites, 11 refs), - R7b N9; + LT1 TF1 IN1 (fix/headstock2); + 4 CM4B0E* fret-number lane ports + 2 CM48EC* Guitarcade ResolveExtraDataColor ports; + LoadStep probes + plain PSARC + [neck] clite GK* (GKH1/GKH2 included)
    assert len([s for s in sites if s.id.startswith("CM")]) == 169
    assert (
        len([s for s in sites if not s.id.startswith("CM") and not s.publish])
        == 465 + 19 + 6 + 31 + 4 + 3 + 7 + 3 - 7 - 26 - 7 - 21 - 2
    )  # 1.0: - C1-C5 N11 GSCL, - the B-phase tuner (15 sites, 11 refs), - the stage-2 peg (PG1-4 PH1 PH2 X_GIVE), - the S7 object (10 sites, 11 refs), - R7b N9; + LT1 TF1 IN1
    assert {s.id for s in sites if s.pitch == "native"} == {
        "PA1",
        "PA2",
        "PM0",
        "PR0",
        "PT1",
        "PM1",
        "PE0",
        "PC1",
        "PC2",
        "PC3",
        "PC4",
    }
    assert not [s.id for s in sites if s.strings == "legacy"]  # the S7 object went in 1.0
    assert sum(s.strings == "native" for s in sites) == 26
    assert sum(s.fretgrid == "native" for s in sites) == 73
    # [neck] native_tuner (core/tuner8.h): its own sites, and the legacy B-phase hooks it replaces
    assert {s.id for s in sites if s.tuner == "native"} == {
        "TU8H",
        "TU8C",
        "TU8A",
        "TU8ALLOC",
        "TU8INIT",
        "TU8FREE",
    } | set(_tuner8_ids())
    assert not [s.id for s in sites if s.tuner == "legacy"]  # the B-phase tuner went in 1.0
    assert sum(s.nextgrid == "native" for s in sites) == 28
    assert not [s.id for s in sites if s.nextgrid == "legacy"]  # R7b / N9 went in 1.0


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


def test_shipped_switches_write_no_legacy_layout_site():
    """The rs7string.ini defaults: native_strings, native_fretgrid, native_nextgrid, native_pitch,
    native_colours, [chords] native_chordloops, native_publish and native_tuner = 1."""
    import sitecheck
    from pathlib import Path

    _, sites = sitetable.load(Path(__file__).resolve().parents[2] / "sites.json")
    shipped = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    written = [s for s in sites if sitecheck.written_under(s, shipped)]
    assert not [s.id for s in written if "legacy" in (s.strings, s.fretgrid, s.nextgrid, s.pitch)]
    assert {s.id for s in written if s.publish == "native"} and {
        s.id for s in written if s.tuner == "native"
    }  # shipped: published and native-tuner sites are written
    assert "GSA1" in {s.id for s in written}  # a native GuitarStrings owner, written by default
