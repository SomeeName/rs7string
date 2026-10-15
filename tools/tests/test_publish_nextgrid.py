"""Published active counts drive the real next-note-grid consumer, without legacy row spawning."""

import struct
import pytest
from test_nextgrid import Machine, run_update, note, RET, STACK, HEAP, FAKE
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX


@pytest.fixture(scope="module")
def native():
    m = Machine("native")
    m.write(m.exports["g_rs7_native_publish"], struct.pack("<I", 1))
    return m


@pytest.mark.parametrize("count", [4, 6, 7, 8])
def test_published_nextgrid_enumerates_each_active_row_once(native, count):
    sets = [(s, 1 + s, note(70 + 10 * s), 1.0) for s in range(8)]
    got = run_update(native, sets, [1.0], strings=count, frets=24)
    assert [args[4] for args in got[0]] == list(range(count))
    assert len(set(got[0])) == count
    assert got[4]  # /GS cookie preserved at every count, including eight
    assert got[1][0] == got[3] + 12  # ret8, caller's frame preserved


def test_published_six_does_not_spawn_an_inactive_row_six(native):
    got = run_update(native, [(6, 5, note(100), 1.0), (7, 4, note(110), 1.0)], [1.0], strings=6)
    assert got[0] == []
