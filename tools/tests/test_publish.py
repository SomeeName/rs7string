"""Publish preparation: switches and verified generated ports, never turn on an unaudited count."""

from pathlib import Path
import itertools
import sitetable
import sitecheck

REPO = Path(__file__).resolve().parents[2]


def test_publish_sites_exist_and_require_all_five_native_layouts():
    sites = sitetable.load(REPO / "sites.json")[1]
    ports = [s for s in sites if getattr(s, "publish", "") == "native"]
    assert ports, "publish ports have not been registered"
    for bits in itertools.product((False, True), repeat=6):
        state = dict(zip(("strings", "fretgrid", "colours", "pitch", "nextgrid", "publish"), bits))
        for s in ports:
            assert sitecheck.written_under(s, state) == all(bits)


def test_publish_off_preserves_every_original_site_selection():
    sites = sitetable.load(REPO / "sites.json")[1]
    for bits in itertools.product((False, True), repeat=5):
        state = dict(zip(("strings", "fretgrid", "colours", "pitch", "nextgrid"), bits))
        expected = [s.id for s in sites if not s.id.startswith("PUB") and sitecheck.written_under(s, state)]
        state["publish"] = False
        assert [s.id for s in sites if sitecheck.written_under(s, state)] == expected


def test_nextgrid_guard_variants_change_only_the_count_immediate():
    sites = {s.id: s for s in sitetable.load(REPO / "sites.json")[1]}
    for sid in ("NN3", "NN4"):
        old, new = sites[sid].write, sites["PUB" + sid].write
        assert new[:-1] == old[:-1]
        assert new[-1] == 8
