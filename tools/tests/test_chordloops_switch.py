from types import SimpleNamespace

import sitecheck


def test_native_chordloops_sites_are_written_by_default():
    # shipped: every switch on ([chords] native_chordloops=1, native_publish=1, native_tuner=1)
    state = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    site = SimpleNamespace(id="CLBF", publish="", strings="", fretgrid="", pitch="", nextgrid="", tuner="")
    assert sitecheck.written_under(site, state)


def test_native_chordloops_sites_are_unwritten_when_switched_off():
    state = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    state["chordloops"] = False
    site = SimpleNamespace(id="CLBF", publish="", strings="", fretgrid="", pitch="", nextgrid="", tuner="")
    assert not sitecheck.written_under(site, state)


def test_chordloops_replaces_legacy_guard_and_seven_slot_latches():
    state = dict.fromkeys((*sitecheck.SWITCH_NAMES, "chordloops"), True)
    for sid in ("N22", "N23", "NF2", "NF3", "CT4", "CT11"):
        site = SimpleNamespace(id=sid, publish="", strings="", fretgrid="", pitch="", nextgrid="")
        assert not sitecheck.written_under(site, state)
