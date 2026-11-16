from types import SimpleNamespace

import sitecheck

_SITE = dict(publish="", strings="", fretgrid="", pitch="", nextgrid="", tuner="")


def test_chordloops_sites_are_written_by_default():
    state = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    assert sitecheck.written_under(SimpleNamespace(id="CLBF", **_SITE), state)


def test_chordloops_sites_need_the_native_strings():
    state = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    state["strings"] = False
    assert not sitecheck.written_under(SimpleNamespace(id="CLBF", **_SITE), state)


def test_the_seven_slot_chord_loop_sites_are_gone():
    import sitetable

    _, sites = sitetable.load(sitecheck.DEFAULT_SITES)
    legacy = {"N22", "N23", "NF2", "NF3", *[f"CT{i}" for i in range(4, 12)]}
    assert not legacy & {s.id for s in sites}
