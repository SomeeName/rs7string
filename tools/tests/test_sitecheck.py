from pathlib import Path

import pytest

import game_files
import sitecheck
import sitetable
from sitetable import Site

ANALYSIS = game_files.ANALYSIS_EXE
SITES = Path(__file__).resolve().parents[2] / "sites.json"  # this checkout


def site(pattern, offset, expect, kind="jmp", length=5, target=None):
    return Site("T", pattern, offset, bytes.fromhex(expect), kind, length, target, "M", "")


def test_unique_match_with_rel8_target():
    text = b"\x00" * 16 + bytes.fromhex("8B 75 0C 74 36 83 FA 05 77 57") + b"\x00" * 16
    ok, msg, va, tgt = sitecheck.check_site(
        text, 0x1000, site("8B 75 0C 74 36 83 FA 05 77 57", 5, "83 FA 05 77 57", target=(4, 1, 5))
    )
    assert ok, msg
    assert va == 0x1000 + 16 + 5
    assert tgt == va + 5 + 0x57


def test_two_matches_fail():
    text = bytes.fromhex("AA BB") * 3
    ok, msg, _, _ = sitecheck.check_site(text, 0, site("AA BB", 0, "AA BB 00 00 00"))
    assert not ok and "3 matches" in msg


def test_expect_mismatch_fails():
    text = bytes.fromhex("11 22 33 44 55 66")
    ok, msg, _, _ = sitecheck.check_site(text, 0, site("11 22", 2, "33 44 55 66 99"))
    assert not ok and "expected" in msg


@pytest.mark.skipif(not ANALYSIS.exists(), reason="analysis exe not built")
def test_every_real_site_resolves_in_the_analysis_exe():
    assert sitecheck.main([str(SITES), str(ANALYSIS)]) == 0


_TEXT = None


def analysis_text():
    """(.text bytes, .text VA) of the analysis exe, loaded once."""
    global _TEXT
    if _TEXT is None:
        import pefile

        pe = pefile.PE(str(ANALYSIS), fast_load=True)
        sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
        _TEXT = (sec.get_data()[: sec.Misc_VirtualSize], pe.OPTIONAL_HEADER.ImageBase + sec.VirtualAddress)
    return _TEXT


def test_ref_site_reports_the_dword_at_the_site():
    text = bytes.fromhex("90 A1 00 0B 36 01 C3")
    ok, msg, va, got = sitecheck.check_site(
        text, 0x1000, site("A1 ?? ?? ?? ?? C3", 1, "00 0B 36 01", kind="ref", length=4)
    )
    assert ok, msg
    assert (va, got) == (0x1002, 0x01360B00)


@pytest.mark.skipif(not ANALYSIS.exists(), reason="analysis exe not built")
@pytest.mark.parametrize(
    "sid,value", [("X_CM", 0x0135F58C), ("X_TCS", 0x01383D74), ("X_THEAD", 0x01360B00), ("X_DDSH", 0x004A81D0)]
)
def test_ref_sites_resolve_to_known_addresses(sid, value):
    text, text_va = analysis_text()
    _, sites = sitetable.load(SITES)
    s = next(x for x in sites if x.id == sid)
    ok, msg, va, got = sitecheck.check_site(text, text_va, s)
    assert ok, msg
    assert got == value
