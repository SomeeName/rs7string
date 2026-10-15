"""Native tuner sites: written only with the native tuner and its layouts. The legacy B-phase tuner sites
were removed in 1.0. A native tuner that is off leaves every other written site set unchanged."""

from pathlib import Path
import itertools

import sitecheck
import sitetable


def _sites():
    return sitetable.load(Path(__file__).resolve().parents[2] / "sites.json")[1]


def _effective(state):
    return state["tuner"] and state["strings"] and state["fretgrid"] and state["colours"]


def test_native_tuner_sites_require_the_switch_and_its_layouts_in_every_configuration():
    sites = _sites()
    native = [s for s in sites if s.tuner == "native"]
    assert len(native) == 6 + 31 and not [s for s in sites if s.tuner == "legacy"]
    assert all(s.id.startswith(("TU8", "TS8")) for s in native)
    for bits in itertools.product((False, True), repeat=len(sitecheck.SWITCH_NAMES)):
        state = dict(zip(sitecheck.SWITCH_NAMES, bits))
        for site in native:
            assert sitecheck.written_under(site, state) == _effective(state)


def test_switch_off_leaves_the_existing_written_site_sets_unchanged():
    sites = _sites()
    old_names = tuple(n for n in sitecheck.SWITCH_NAMES if n != "tuner")
    for bits in itertools.product((False, True), repeat=len(old_names)):
        state = dict(zip(old_names, bits))
        before = {s.id for s in sites if s.tuner != "native" and sitecheck.written_under(s, {**state, "tuner": False})}
        after = {s.id for s in sites if sitecheck.written_under(s, {**state, "tuner": False})}
        assert after == before
