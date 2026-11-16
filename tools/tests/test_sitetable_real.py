"""Real site metadata against compiled registration macros and their generator-owned includes."""

import re

import pytest
import sitetable

from emu_common import SITES
from registration_harness import registered_rows


@pytest.fixture(scope="module")
def rows(tmp_path_factory):
    return registered_rows(tmp_path_factory.mktemp("registrations"))


def _sites():
    return sitetable.load(SITES)[1]


def test_real_sites_json_checksum():
    checksum, _ = sitetable.load(SITES)
    assert checksum == 0x00B13D7C


def test_every_site_has_exactly_one_registered_owner(rows):
    sites = _sites()
    # Fast mode is an early byte patch with its own writer, rather than a hook-table row.
    fast = {s.id for s in sites if s.group == "fast"}
    assert fast == {"FST_REVIEW"}
    assert set(rows) == {s.id for s in sites} - fast
    for site in sites:
        if site.id in fast:
            assert site.kind == "bytes"
        else:
            assert rows[site.id].startswith("Rs7_Ref(") == (site.kind in sitetable.REF_KINDS), site.id


@pytest.mark.parametrize(
    "field,gate",
    [
        ("strings", "Strings"),
        ("fretgrid", "Fretgrid"),
        ("nextgrid", "Nextgrid"),
        ("pitch", "Pitch"),
        ("publish", "Publish"),
        ("tuner", "Tuner"),
    ],
)
def test_native_site_metadata_matches_registration_gates(rows, field, gate):
    registered = {
        sid
        for sid, row in rows.items()
        if re.search(rf"kRs7Gate{gate}\b", row) or (field == "nextgrid" and row.startswith("NgHook("))
    }
    assert registered, f"no registered {field} sites found"
    assert {s.id for s in _sites() if getattr(s, field) == "native"} == registered
    assert not [s.id for s in _sites() if getattr(s, field) == "legacy"]


def test_shipped_switches_write_no_legacy_layout_site():
    """All native layout switches are shipped on; legacy layout sites must never be written."""
    import sitecheck

    shipped = dict.fromkeys(sitecheck.SWITCH_NAMES, True)
    written = [s for s in _sites() if sitecheck.written_under(s, shipped)]
    assert not [s.id for s in written if "legacy" in (s.strings, s.fretgrid, s.nextgrid, s.pitch)]
    assert {s.id for s in written if s.publish == "native"}
    assert {s.id for s in written if s.tuner == "native"}
    assert "GSA1" in {s.id for s in written}
