"""Execute built serializer tails/ABI in Unicorn, mocking only native JSON services.

No game process. Captures the actual GamePut output from the DLL, checks T8/stock
pointer routes, and checks the serializer stubs' stack/register restoration.
"""

import struct
from contextlib import contextmanager
import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
from emu_harness import ALL_REGS, DATA, RET_MAGIC, STACK, Emu, ns_set, site_va

BASE = DATA + 0x70000
FAKE = BASE + 0x1000


def u32(emu, at):
    return struct.unpack("<I", emu.read(at, 4))[0]


def cstr(emu, at):
    return emu.read(at, 100).split(b"\0")[0].decode()


@contextmanager
def json_sink(emu):
    addresses = {"key": 0x87C470, "num": 0x87F930, "set": 0x8816C0, "drop": 0x87C3D0}
    saved = {a: emu.read(a, 3) for a in addresses.values()}
    values = {}
    numeric = {}
    alloc = [BASE + 0x5000]

    def intercept(uc, a, n, user):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if a == addresses["key"]:
            uc.mem_write(u32(emu, esp + 4), struct.pack("<I", uc.reg_read(UC_X86_REG_ECX)))
        elif a == addresses["num"]:
            ptr = alloc[0]
            alloc[0] += 0x20
            numeric[ptr] = struct.unpack("<d", emu.read(esp + 4, 8))[0]
            uc.mem_write(ptr, struct.pack("<I", BASE + 0x2000))
            uc.mem_write(uc.reg_read(UC_X86_REG_EDI), struct.pack("<I", ptr))
        elif a == addresses["set"]:
            assert u32(emu, esp + 4) == BASE + 0x3000
            name = cstr(emu, u32(emu, u32(emu, esp + 8)))
            number = u32(emu, u32(emu, esp + 12))
            values[name] = int(numeric[number])

    hooks = []
    emu.uc.mem_write(BASE + 0x2000, struct.pack("<II", FAKE, FAKE))
    emu.uc.mem_write(FAKE, b"\xc3")
    try:
        for name, a in addresses.items():
            emu.uc.mem_write(a, b"\xc2\x04\x00" if name == "key" else b"\xc2\x0c\x00" if name == "set" else b"\xc3")
            hooks.append(emu.uc.hook_add(UC_HOOK_CODE, intercept, begin=a, end=a))
        emu.uc.mem_write(emu.exports["g_rs7_diagrams"], struct.pack("<I", 1))
        yield values
    finally:
        for h in hooks:
            emu.uc.hook_del(h)
        for a, b in saved.items():
            emu.uc.mem_write(a, b)
        emu.uc.mem_write(emu.exports["g_rs7_diagrams"], b"\0" * 4)
        ns_set(emu, [])


@pytest.mark.parametrize("count", [6, 7, 8])
@pytest.mark.parametrize("stock_pointer", [False, True])
def test_loaded_serializer_tail_real_output(emus, count, stock_pointer):
    _, emu = emus
    with json_sink(emu) as values:
        t8 = BASE + 0x4000
        stock = BASE + 0x4800
        holder = BASE + 0x4900
        t = bytearray(0x80)
        t[0x48:0x58] = b"\xff" * 16
        t[0x78] = count
        t[0x48] = 0
        t[0x48 + 6] = 12
        t[0x50 + 6] = 0
        if count == 8:
            t[0x48 + 7] = 14
            t[0x50 + 7] = 2
        emu.uc.mem_write(t8, bytes(t))
        emu.uc.mem_write(holder, struct.pack("<I", BASE + 0x3000))
        ns_set(emu, [(stock, 1, t8, 0, 0, 0, 1, 0)])
        sp = STACK + 0x9000
        bp = sp + 0x500
        emu.uc.mem_write(bp + 8, struct.pack("<II", holder, stock if stock_pointer else t8))
        emu.uc.mem_write(sp + 0x180, struct.pack("<I", 0x12345678))
        regs = {r: 0x200 + i for i, r in enumerate(ALL_REGS)}
        regs.update({UC_X86_REG_ESP: sp, UC_X86_REG_EBP: bp})
        emu.run(site_va("DGS"), site_va("DGS") + 7, regs)
        assert emu.uc.reg_read(UC_X86_REG_ESP) == sp
        for r in ALL_REGS:
            if r not in (UC_X86_REG_ECX, UC_X86_REG_EIP):
                assert emu.uc.reg_read(r) == regs[r]
        assert emu.uc.reg_read(UC_X86_REG_ECX) == 0x12345678
        if count == 6:
            assert values == {}
        else:
            assert values["StartFret"] == 12 and values["LowEFret"] == 0
            assert values["LowBFret"] == 1 and values["LowB_Finger"] == 0 and values["LowB_Color"] == 0x808080
            assert ("LowFsFret" in values) == (count == 8)
            if count == 8:
                assert values["LowFsFret"] == 3 and values["LowFs_Finger"] == 2


@pytest.mark.parametrize("count", [6, 7, 8])
def test_manifest_serializer_tail_real_output_and_capo(emus, count):
    _, emu = emus
    with json_sink(emu) as values:
        # Numeric node virtual +54 returns itself (number at +8).
        nodevt = BASE + 0x2100
        numberfn = FAKE + 0x10
        emu.uc.mem_write(nodevt + 0x54, struct.pack("<I", numberfn))
        emu.uc.mem_write(numberfn, b"\x8b\xc1\xc3")
        arrays = []
        for col in range(2):
            arr = BASE + 0x6000 + col * 0x100
            begin = arr + 0x40
            emu.uc.mem_write(arr + 0x10, struct.pack("<II", begin, begin + 4 * count))
            arrays.append(arr)
            for i in range(count):
                node = BASE + 0x7000 + col * 0x200 + i * 0x20
                v = 2 if i == 0 and col == 0 else 12 if i == 6 and col == 0 else 1 if i == 6 and col == 1 else -1
                emu.uc.mem_write(node, struct.pack("<II d", nodevt, 1, float(v)))
                emu.uc.mem_write(begin + i * 4, struct.pack("<I", node))
        sp = STACK + 0xA000
        bp = sp + 0x500
        emu.uc.mem_write(bp + 0xC, b"\0" * 8)  # no associated loaded manifest for this synthetic fixture
        emu.uc.mem_write(sp + 0x50, struct.pack("<I", BASE + 0x3000))
        emu.uc.mem_write(sp + 0x58, struct.pack("<II", arrays[1], arrays[0]))
        emu.uc.mem_write(sp + 0x3F, b"\2")
        emu.run(site_va("DGM"), site_va("DGM") + 7, {UC_X86_REG_ESP: sp, UC_X86_REG_EBP: bp})
        assert emu.uc.reg_read(UC_X86_REG_ESP) == sp + 4
        assert emu.uc.reg_read(UC_X86_REG_ECX) == sp + 4 + 0x74
        if count == 6:
            assert values == {}
        else:
            assert values["StartFret"] == 12 and values["LowEFret"] == 0
            assert values["LowBFret"] == 1 and values["LowB_Finger"] == 1
            if count == 8:
                assert values["LowFsFret"] == -1 and "LowFs_Finger" not in values


@pytest.mark.parametrize("on", [0, 1])
@pytest.mark.parametrize("index", [0, 1, 2])
def test_tp13_resolves_registered_t8_and_preserves_stride_registers(emus, on, index):
    _, emu = emus
    stock = BASE + 0x9000
    t8 = BASE + 0xA000
    song = BASE + 0xB000
    ns_set(emu, [(stock, 2, t8, 0, 0, 0, song, 0)])
    emu.uc.mem_write(song + 0x94, struct.pack("<I", stock))
    emu.uc.mem_write(emu.exports["g_rs7_diagrams"], struct.pack("<I", on))
    try:
        emu.run(
            site_va("TP13"),
            site_va("TP13") + 12,
            {UC_X86_REG_EAX: index, UC_X86_REG_ESI: song, UC_X86_REG_ESP: STACK + 0x8000},
        )
        want = t8 + index * 0x80 if on and index < 2 else stock + index * 0x48
        assert emu.uc.reg_read(UC_X86_REG_EAX) == want
        assert emu.uc.reg_read(UC_X86_REG_EDX) == stock
        assert emu.uc.reg_read(UC_X86_REG_ECX) == index * 9
    finally:
        ns_set(emu, [])
        emu.uc.mem_write(emu.exports["g_rs7_diagrams"], b"\0" * 4)


def test_dgb_passes_exact_song_and_uuid_attributes_then_replays_registers(emus):
    _, emu = emus
    sid = "DGB"
    fn = emu.exports["Rs7Diagram_Bind"]  # called by the DGB mid-hook (features/diagrams/hooks_mid.cpp)
    saved = emu.read(fn, 1)
    seen = []
    sp = STACK + 0xC000
    bp = sp + 0x500
    emu.uc.mem_write(sp + 0x50, struct.pack("<I", 0x12345678))
    emu.uc.mem_write(bp + 8, struct.pack("<I", 0x87654321))

    def capture(uc, a, n, user):
        stack = uc.reg_read(UC_X86_REG_ESP)
        seen.append((u32(emu, stack + 4), u32(emu, stack + 8)))

    h = emu.uc.hook_add(UC_HOOK_CODE, capture, begin=fn, end=fn)
    emu.uc.mem_write(fn, b"\xc3")
    try:
        emu.run(site_va(sid), site_va(sid) + 11, {UC_X86_REG_ESP: sp, UC_X86_REG_EBP: bp})
        assert seen == [(0x87654321, 0x12345678)]
        assert emu.uc.reg_read(UC_X86_REG_ESP) == sp
        assert emu.uc.reg_read(UC_X86_REG_ESI) == 0x12345678
        assert emu.uc.reg_read(UC_X86_REG_ECX) == sp + 0x1B8
    finally:
        emu.uc.hook_del(h)
        emu.uc.mem_write(fn, saved)
