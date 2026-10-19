"""N1 (0x5965CA, type-4 string pool request) and N21 (0x5965E9, type-2 ghost pool request): native_publish preloads 8 GuitarStrings slots, so the pool must
hold 8 instances (a pool of 7 left the 8th take null and held every song load: tests/pubfix run2)."""

import struct
import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
from emu_harness import Emu, DATA, STACK, DLL, SNAPSHOT, site_va

pytestmark = pytest.mark.skipif(not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs DLL/snapshot")


@pytest.fixture(scope="module")
def emu():
    return Emu(True)  # the sites written as the DLL writes them: N1 / N21 jump into their mid-hook entries


@pytest.mark.parametrize(
    "count,s7,ports,want", [(6, 0, 0, 6), (6, 1, 0, 7), (6, 0, 1, 8), (6, 1, 1, 8), (4, 1, 1, 4), (4, 0, 0, 4)]
)
@pytest.mark.parametrize("site,reg", [("N1", UC_X86_REG_EAX), ("N21", UC_X86_REG_ECX)])
def test_pool_request_count(emu, site, reg, count, s7, ports, want):
    e = emu
    put = lambda a, v: e.uc.mem_write(a, struct.pack("<I", v))
    put(e.exports["g_rs7_s7_on"], s7)
    put(e.exports["g_rs7_publish_ports"], ports)
    profile = DATA + 0x100
    e.uc.mem_write(profile + 0x38, bytes([count]))
    va = site_va(site)
    try:
        e.run(va, va + 7, {reg: profile, UC_X86_REG_ESP: STACK + 0x80000})
        seen = e.uc.reg_read(reg)
    finally:
        put(e.exports["g_rs7_s7_on"], 0)
        put(e.exports["g_rs7_publish_ports"], 0)
    assert seen == want
