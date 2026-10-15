"""Stage 5 ColourManager: exact address ports, stock-column equivalence."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gen_colournative as cm


def test_layout_preserves_every_stock_state_and_mode():
    seen = set()
    for base, new_base in ((0x350, 0x350), (0x890, 0xA50)):
        for mode in range(2):
            for state in range(28):
                for slot in range(6):
                    old = base + 4 * ((mode * 28 + state) * 6 + slot)
                    new = cm.move(old)
                    assert new == new_base + 4 * ((mode * 28 + state) * 8 + slot)
                    assert new not in seen
                    seen.add(new)


def test_six_key_walk_and_auxiliary_bounds_are_not_widened():
    ports = cm.ports()
    addresses = {p.va for p in ports}
    assert not addresses & {0x518AFE, 0x518B03, 0x59AE73, 0x59AEC5}
    assert {0x518B1E, 0x51971F, 0x518AC4, 0x518571} <= addresses


def test_generated_sites_are_current():
    import json

    actual = json.loads((cm.REPO / "sites.json").read_text())["sites"]
    assert [s for s in actual if s["id"].startswith("CM")] == cm.sites()
