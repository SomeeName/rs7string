"""Bounded emulator runs must reach their requested stop, even after producing correct output."""

import struct

import pytest
from unicorn import UC_ARCH_X86, UC_MODE_32, Uc

import emu_common


@pytest.mark.parametrize("tail", [b"\x90", b"\xeb\xfe"])
def test_bounded_run_requires_the_stop_address(tail):
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x1000, 0x1000)
    # Write the expected result, then either finish or loop forever.
    code = b"\xc7\x05" + struct.pack("<II", 0x1800, 42) + tail
    uc.mem_write(0x1000, code)
    if tail == b"\xeb\xfe":
        with pytest.raises(AssertionError, match="stopped at"):
            emu_common.run_checked(uc, 0x1000, 0x1000 + len(code), count=20)
    else:
        emu_common.run_checked(uc, 0x1000, 0x1000 + len(code), count=20)
    assert bytes(uc.mem_read(0x1800, 4)) == struct.pack("<I", 42)
