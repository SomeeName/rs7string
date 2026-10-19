"""Stage 5 ColourManager: exact address ports, stock-column equivalence."""

import gen_colournative as cm
from emu_common import needs_exe


def test_layout_preserves_every_stock_state_and_mode():
    seen = set()
    for base, new_base in ((0x350, 0xDD0), (0x890, 0x14D0)):
        for mode in range(2):
            for state in range(28):
                for slot in range(6):
                    old = base + 4 * ((mode * 28 + state) * 6 + slot)
                    new = cm.move(old)
                    assert new == new_base + 4 * ((mode * 28 + state) * 8 + slot)
                    assert new not in seen
                    seen.add(new)


@needs_exe
def test_six_key_walk_and_auxiliary_bounds_are_not_widened():
    ports = cm.ports()
    addresses = {p.va for p in ports}
    assert not addresses & {0x518AFE, 0x518B03, 0x59AE73, 0x59AEC5}
    assert {0x518B1E, 0x51971F, 0x518AC4, 0x518571} <= addresses


@needs_exe
def test_generated_sites_are_current():
    import json

    actual = json.loads((cm.REPO / "sites.json").read_text())["sites"]
    assert [s for s in actual if s["id"].startswith("CM")] == cm.sites()


def test_native_tables_leave_the_six_column_rsmods_abi_intact():
    # RSMods uses offsets through 0xDD0 without calling a game getter.
    assert cm.move(0x350) >= 0xDD0
    assert cm.move(0x890) >= cm.move(0x350) + 2 * 28 * 8 * 4
