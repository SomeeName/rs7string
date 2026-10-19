"""The real sites.json: every site id in order, switch classes (strings, fretgrid, tuner, nextgrid) and shipped defaults."""

from pathlib import Path

import pytest
import sitetable
from emu_common import needs_exe


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


SITES_JSON = Path(__file__).resolve().parents[2] / "sites.json"

# The sites before the generated FretGrid block, in sites.json order (CM* colour ports excluded).
_HAND_WRITTEN_IDS = [
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
    "PGH1",
    "GLW",
    "TF1",
    "IN1",
    "X_EULER",
    # native N-slot PegSet (features/headstock, issue #18 stage 3)
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
    # the 7-string tuner (features/tuner/hooks.h)
    "X_TMGR",
    "X_GETDET",
    "W",
    "X_WTHR",
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
    # edited headstock model (features/headstock/assetload.h)
    "X_FIXSTR",
    "AL1",
    "X_STRPARAMS",
    # the 7-string model's textures, the 7th knob's tint (issue #17)
    "X_DDSH",
    "X_NFCTOR",
    "X_NFDTOR",
    # virtual guitarist ([test] vguitar, features/vguit/vguit.h)
    "X_VGSLOT",
    "X_VGDI",
    "TICK",
    "JUD1",
    "JUD2",
    "AUD1",
    # M4 chords with a string-6 column (features/chords/chords.h)
    "B1",
    "X_SST",
    "B2",
    # seven-string tuning names (features/tuner/tuningname.h, issue #15)
    "TJ",
    "TU",
    "TUT",
    "TN",
    "X_STRCON",
    "X_STRASG",
    "X_LOC",
    "X_STRDTOR",
    # song end log (features/songend/songend.h, issue #13)
    "SE1",
    "SE2",
    "SE3",
    # [test] force_* variants (features/testdrive/testdrive.h)
    "TVCB",
    "TVGP",
    # RS7X SNG extension block (model/extblock.h, issue #18)
    "XB1",
    # native N-slot chord data (features/nslot/nslot.h, issue #18 stage 2)
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
    # native N-slot chord detection (features/nslot/match.h, tracker eval tails)
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
    # native N-slot chord tracking (features/nslot/tracker.h)
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
    # native N-slot GuitarStrings (features/strings/strings.h, issue #18 stage 4)
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
]
# [neck] native_pitch (features/pitch/pitch.h, issue #18 stage 5 area 3)
_PITCH_IDS = [
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
]
_PUBLISH_IDS = ["PUBCL", "PUBTO", "PUBWU", "PUBNN1", "PUBNN3", "PUBNN4"] + [
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
]
_CLITE_IDS = [
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
]
_TUNER8_CORE_IDS = ["TU8H", "TU8C", "TU8A", "TU8ALLOC", "TU8INIT", "TU8FREE"]
_CLITE_GK_IDS = [
    "GKC",
    "GKE",
    "GKM",
    "GKL",
    "GKT",
    "GKH1",
    "GKH2",
]


def _sites():
    return sitetable.load(SITES_JSON)[1]


def _expected_order():
    return (
        _HAND_WRITTEN_IDS
        + _fretgrid_ids()
        + _PITCH_IDS
        + _nextgrid_ids()
        + _PUBLISH_IDS
        + _CLITE_IDS
        + _TUNER8_CORE_IDS
        + _tuner8_ids()
        + _CLITE_GK_IDS
        + ["LSA", "LSG", "LSC", "LSE"]
        + ["PP_SNGD", "PP_SNGF", "PP_TOC"]  # plain PSARC (group psarc)
        + ["FST_REVIEW"]  # [test] fast (group fast)
    )


def test_real_sites_json_checksum():
    checksum, _ = sitetable.load(SITES_JSON)
    assert checksum == 0x00B13D7C


@needs_exe
def test_site_ids_in_order():
    assert [s.id for s in _sites() if not s.id.startswith("CM")] == _expected_order()


def test_site_counts():
    sites = _sites()
    assert (
        len([s for s in sites if not s.publish])
        == 626 + 19 + 6 + 31 + 4 + 3 + 7 + 3 + 4 + 2 - 7 - 26 - 7 - 21 - 2 + 2 + 1 + 1 + 1 + 1 + 2
    )  # X_WTHR (W's threshold); GLW (RSMods pure red glow); PGH1 (peg glow of hidden 3+3 parts); 1.0: - C1-C5 N11 GSCL, - the B-phase tuner (15 sites, 11 refs), - the stage-2 peg (PG1-4 PH1 PH2 X_GIVE), - the S7 object (10 sites, 11 refs), - R7b N9; + LT1 TF1 IN1 (fix/headstock2); + 4 CM4B0E* fret-number lane ports + 2 CM48EC* Guitarcade ResolveExtraDataColor ports; + LoadStep probes + plain PSARC + [neck] clite GK* (GKH1/GKH2 included); + TVCB TVGP ([test] force_*)
    assert len([s for s in sites if s.id.startswith("CM")]) == 169
    assert (
        len([s for s in sites if not s.id.startswith("CM") and not s.publish])
        == 465 + 19 + 6 + 31 + 4 + 3 + 7 + 3 - 7 - 26 - 7 - 21 - 2 + 1 + 1 + 1 + 1 + 2
    )  # X_WTHR; GLW; PGH1; 1.0: - C1-C5 N11 GSCL, - the B-phase tuner (15 sites, 11 refs), - the stage-2 peg (PG1-4 PH1 PH2 X_GIVE), - the S7 object (10 sites, 11 refs), - R7b N9; + LT1 TF1 IN1; + TVCB TVGP


def test_native_pitch_sites():
    assert {s.id for s in _sites() if s.pitch == "native"} == set(_PITCH_IDS)


def test_native_strings_and_fretgrid_sites():
    sites = _sites()
    assert not [s.id for s in sites if s.strings == "legacy"]  # the S7 object went in 1.0
    assert sum(s.strings == "native" for s in sites) == 26
    assert sum(s.fretgrid == "native" for s in sites) == 73


def test_native_tuner_sites():
    sites = _sites()
    # [neck] native_tuner (features/tuner/hooks.h): its own sites, and the legacy B-phase hooks it replaces
    assert {s.id for s in sites if s.tuner == "native"} == set(_TUNER8_CORE_IDS) | set(_tuner8_ids())
    assert not [s.id for s in sites if s.tuner == "legacy"]  # the B-phase tuner went in 1.0


def test_native_nextgrid_sites():
    sites = _sites()
    assert sum(s.nextgrid == "native" for s in sites) == 28
    assert not [s.id for s in sites if s.nextgrid == "legacy"]  # R7b / N9 went in 1.0


def test_shipped_switches_write_no_legacy_layout_site():
    """The rs7string.ini defaults: native_strings, native_fretgrid, native_nextgrid, native_pitch,
    native_colours, [chords] native_chordloops, native_publish and native_tuner = 1."""
    import sitecheck

    sites = _sites()
    shipped = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    written = [s for s in sites if sitecheck.written_under(s, shipped)]
    assert not [s.id for s in written if "legacy" in (s.strings, s.fretgrid, s.nextgrid, s.pitch)]
    assert {s.id for s in written if s.publish == "native"} and {
        s.id for s in written if s.tuner == "native"
    }  # shipped: published and native-tuner sites are written
    assert "GSA1" in {s.id for s in written}  # a native GuitarStrings owner, written by default
