"""Native N-slot GuitarStrings copies (tools/gen_fncopy.py, features/strings/strings.h).

The generated header must match the unpacked exe; a copy must behave like the stock function on a
native GuitarStrings with six strings and drive string 6 too when the count is seven (Unicorn).
"""

from __future__ import annotations

import struct

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_ESP

import gen_fncopy
from emu_common import needs_exe

pytestmark = needs_exe


def test_generated_header_is_current():
    text, base = gen_fncopy.load_text()
    results = []
    for fn in gen_fncopy.FNS:
        code, edits, rel32, pro, insns = gen_fncopy.build(fn, text, base)
        results.append((fn, code, edits, rel32, pro, insns))
    assert gen_fncopy.render(results) == gen_fncopy.OUT.read_text(encoding="utf-8")


def test_every_count_byte_of_a_counted_copy_is_moved():
    text, base = gen_fncopy.load_text()
    for fn in gen_fncopy.FNS:
        if not fn.count:
            continue
        code, edits, _, _, insns = gen_fncopy.build(fn, text, base)
        moved = {e.off for e in edits if e.write}
        for i in insns:
            for op in i.operands:
                if op.type == 3 and op.size == 1 and op.mem.disp == 0xB and op.mem.index == 0 and op.mem.base:
                    assert i.address - fn.start in moved, f"{fn.id} {i.address:08X} not moved"


# ---- ResetState 0x59AC20 (EDI = GuitarStrings): stock vs copy ----
GS, INFO, OWNER, KIDS = 0x10000, 0x11000, 0x12000, 0x20000
CODE_STOCK, CODE_COPY, STACK, RET = 0x00400000, 0x30000000, 0x31000000, 0x32000000


def build_copy(fid: str) -> tuple[gen_fncopy.Fn, bytes]:
    text, base = gen_fncopy.load_text()
    fn = next(f for f in gen_fncopy.FNS if f.id == fid)
    code, edits, rel32, _, _ = gen_fncopy.build(fn, text, base)
    buf = bytearray(code)
    for e in edits:
        assert not e.sym, "this test runs copies without DLL calls"
        buf[e.off : e.off + len(e.write)] = e.write
    for off, tgt in rel32:
        buf[off : off + 4] = struct.pack("<i", tgt - (CODE_COPY + off + 4))
    return fn, bytes(buf)


def run_reset(native: bool, count: int, loaded: int):
    text, base = gen_fncopy.load_text()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for a, n in (
        (GS, 0x1000),
        (INFO, 0x1000),
        (OWNER, 0x1000),
        (KIDS, 0x10000),
        (STACK - 0x10000, 0x20000),
        (RET, 0x1000),
    ):
        uc.mem_map(a, n, UC_PROT_ALL)
    uc.mem_write(RET, b"\xf4")  # hlt
    fn, copy = build_copy("GSRS")
    if native:
        uc.mem_map(CODE_COPY, 0x10000, UC_PROT_ALL)
        uc.mem_write(CODE_COPY, copy)
        entry = CODE_COPY
    else:
        uc.mem_map(CODE_STOCK, (len(text) + 0x2000) & ~0xFFF, UC_PROT_ALL)
        uc.mem_write(base, text)
        entry = fn.start
    kid = [KIDS + 0x200 * i for i in range(16)]  # main 0..7, ghost 8..15; entity +4 = 0: no calls
    for k in kid:
        uc.mem_write(k, b"\x00" * 0x110)
        uc.mem_write(k + 0x70, b"\x01\x01")  # dimming on, dimmed
        uc.mem_write(k + 0x78, struct.pack("<f", 3.0))
        uc.mem_write(k + 0x6C, b"\x01")
    uc.mem_write(GS, struct.pack("<I", OWNER))
    uc.mem_write(OWNER + 0x24, struct.pack("<I", INFO))
    uc.mem_write(INFO + 0x0B, bytes([count]))
    for s in range(6):
        uc.mem_write(GS + 4 + 4 * s, struct.pack("<I", kid[s]))
        uc.mem_write(GS + 0x1C + 4 * s, struct.pack("<I", kid[8 + s]))
    if native:
        for s in range(8):
            uc.mem_write(GS + 0x40 + 4 * s, struct.pack("<I", kid[s]))
            uc.mem_write(GS + 0x60 + 4 * s, struct.pack("<I", kid[8 + s]))
        uc.mem_write(INFO + 0x40, bytes([count, loaded]))
    uc.mem_write(STACK, struct.pack("<I", RET))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.reg_write(UC_X86_REG_EDI, GS)
    uc.emu_start(entry, RET, count=100000)
    return [bytes(uc.mem_read(k + 0x6C, 0x10)) for k in kid]


def test_reset_copy_equals_stock_for_six_strings():
    stock = run_reset(False, 6, 6)
    copy = run_reset(True, 6, 6)
    assert copy == stock
    assert stock[6] == run_reset(False, 6, 6)[6] and stock[6][0x05] == 1  # string 6 untouched (still dimmed)


def test_reset_copy_resets_string_six_with_seven_loaded():
    copy = run_reset(True, 6, 7)
    stock = run_reset(False, 6, 6)
    assert copy[:6] == stock[:6] and copy[8:14] == stock[8:14]
    assert copy[6] == stock[0]  # main string 6 reset like string 0
    assert copy[14] == stock[8]  # its ghost like ghost 0
    assert copy[7] != stock[0]  # string 7 (not loaded) untouched


# ---- UpdateTransforms 0x59A920 (GSXF; EDI = owner, arg1 = GuitarStrings): pluck swing euler per string ----
OWNER_XF, INFO_XF, ENTITIES = 0x13000, 0x14000, 0x40000
CALL_POS, CALL_ROT = 0x007ADF70, 0x007AE1A0  # position / rotation setters, replaced by a `ret` that records
EULER_FLIP, EULER_DEFAULT = 0x01224C0C, 0x013610C8  # 180.0f / the zero default triple
STOCK_MODES = [1, 1, 1, 0, 0, 0]  # perStringMode of the stock guitar: strings 0-2 turned 180 degrees about y
FLIPPED, UNROTATED = (0.0, 180.0, 0.0), (0.0, 0.0, 0.0)


def run_transforms(native: bool, count: int, modes: list[int], left_handed: bool = False):
    """The euler triple each rotation call received, per (string, main/ghost) in call order."""
    text, base = gen_fncopy.load_text()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for a, n in (
        (GS, 0x1000),
        (OWNER_XF, 0x1000),
        (INFO_XF, 0x1000),
        (KIDS, 0x10000),
        (ENTITIES, 0x10000),
        (STACK - 0x10000, 0x20000),
        (RET, 0x1000),
        (EULER_FLIP & ~0xFFF, 0x1000),
        (EULER_DEFAULT & ~0xFFF, 0x1000),
    ):
        uc.mem_map(a, n, UC_PROT_ALL)
    uc.mem_write(RET, b"\xf4")  # hlt
    uc.mem_write(EULER_FLIP, struct.pack("<f", 180.0))
    fn, copy = build_copy("GSXF")
    if native:
        uc.mem_map(CALL_POS & ~0xFFF, 0x2000, UC_PROT_ALL)
        uc.mem_map(CODE_COPY, 0x10000, UC_PROT_ALL)
        uc.mem_write(CODE_COPY, copy)
        entry = CODE_COPY
    else:
        entry = fn.start
        uc.mem_map(base, (len(text) + 0xFFF) & ~0xFFF, UC_PROT_ALL)
        uc.mem_write(base, text)
    uc.mem_write(CALL_POS, b"\xc3")  # the setters become a bare `ret` (the rotation one is recorded below)
    uc.mem_write(CALL_ROT, b"\xc3")
    kid = [KIDS + 0x200 * i for i in range(16)]  # main 0..7, ghost 8..15
    for i, k in enumerate(kid):
        uc.mem_write(k + 4, struct.pack("<I", ENTITIES + 0x100 * i))
    uc.mem_write(OWNER_XF + 0x24, struct.pack("<I", INFO_XF))
    for s, m in enumerate(modes[:6]):
        uc.mem_write(INFO_XF + 0x20 + 4 * s, struct.pack("<I", m))
    uc.mem_write(INFO_XF + 0x09, bytes([int(left_handed)]))
    uc.mem_write(INFO_XF + (0x41 if native else 0x0B), bytes([count]))
    main_off, ghost_off = (0x40, 0x60) if native else (0x04, 0x1C)
    for s in range(min(count, 8 if native else 6)):
        uc.mem_write(GS + main_off + 4 * s, struct.pack("<I", kid[s]))
        uc.mem_write(GS + ghost_off + 4 * s, struct.pack("<I", kid[8 + s]))
    seen = []

    def on_rot(uc_, addr, size, _data):
        esp = uc_.reg_read(UC_X86_REG_ESP)
        ent, euler = struct.unpack("<II", bytes(uc_.mem_read(esp + 4, 8)))
        seen.append((ent, struct.unpack("<3f", bytes(uc_.mem_read(euler, 12)))))

    uc.hook_add(UC_HOOK_CODE, on_rot, begin=CALL_ROT, end=CALL_ROT)
    uc.mem_write(STACK, struct.pack("<II", RET, GS))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.reg_write(UC_X86_REG_EDI, OWNER_XF)
    uc.emu_start(entry, RET, count=100000)
    assert len(seen) == 2 * count
    return [e for _, e in seen[0::2]], [e for _, e in seen[1::2]]  # main, ghost per string


def test_transforms_copy_equals_stock_for_six_strings():
    stock = run_transforms(False, 6, STOCK_MODES)
    assert stock[0] == [FLIPPED] * 3 + [UNROTATED] * 3  # the premise of the test
    assert run_transforms(True, 6, STOCK_MODES) == stock


def test_transforms_extra_strings_take_string_zero_mode():
    # Strings 6 and 7 (the seventh and eighth) turn like string 0 (swing toward the neck's middle), main and ghost.
    main, ghost = run_transforms(True, 8, STOCK_MODES)
    assert main[6] == main[7] == main[0] == FLIPPED
    assert ghost[6] == ghost[7] == ghost[0] == FLIPPED
    assert main[:6] == [FLIPPED] * 3 + [UNROTATED] * 3  # strings 0-5 as in the stock function


def test_transforms_extra_strings_follow_whatever_mode_string_zero_has():
    main, ghost = run_transforms(True, 8, [0, 1, 1, 0, 0, 0])  # e.g. an inverted layout
    assert main[0] == ghost[0] == UNROTATED
    assert main[6] == main[7] == ghost[6] == ghost[7] == UNROTATED
    main, ghost = run_transforms(True, 8, [1, 0, 0, 0, 0, 0])
    assert main[6] == main[7] == ghost[6] == ghost[7] == FLIPPED
