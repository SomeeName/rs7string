"""Machine for the next-note-grid tests: stock or native (DLL + NG sites) game code in Unicorn, grid and Update runners."""

from __future__ import annotations

import gc
import random
import struct
from functools import lru_cache

import capstone
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

import gen_fncopy
import gen_nextgrid as ng
import sitecheck
import sitetable
from emu_common import CRT_MEMSET, SITES, crt_memset, exe_sections, load_dll, stub_security_cookie
from emu_harness import MID_CALLBACKS, build_mid_entry

GRID, STACK, RET = 0x10001000, 0x12000000, 0x13000000
HEAP, FAKE = 0x14000000, 0x15000000
NODES, SESS, NOTE_BASE = HEAP, HEAP + 0x1000, HEAP + 0x4000
FAKE_PTR = (
    HEAP + 0xF000
)  # data cell: where the REGISTER trampoline jumps (code is never rewritten: Unicorn keeps stale TBs)
STACK_SIZE = 0x20000
TOP = STACK + STACK_SIZE - 0x1000  # [esp] at the entry of Update
SET, INIT, PRUNE, REGISTER, SPAWN, UPDATE, TREE_INSERT = (
    0x7AA140,
    0x7AA0B0,
    0x7AA1B0,
    0x7A5D50,
    0x7A56F0,
    0x7E4870,
    0x7AA260,
)
TREE_ERASE = 0x7D06C0
ROW, CELLS = 0x68, 26


def f2u(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def note(k: int) -> int:
    return NOTE_BASE + 0x100 * k


@lru_cache(maxsize=1)
def game_text():
    return gen_fncopy.load_text()


@lru_cache(maxsize=1)
def site_list():
    return sitetable.load(SITES)[1]


class Machine:
    def __init__(self, mode: str):
        assert mode in ("stock", "native")
        gc.collect()  # each Uc commits tens of MB: free the previous ones first
        self.mode = mode
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for va, data in exe_sections():
            uc.mem_map(va, (len(data) + 0xFFF) & ~0xFFF, UC_PROT_ALL)
            uc.mem_write(va, data)
        for a, n in ((GRID - 0x1000, 0x20000), (STACK, STACK_SIZE), (RET, 0x1000), (HEAP, 0x10000), (FAKE, 0x100000)):
            uc.mem_map(a, n, UC_PROT_ALL)
        uc.mem_write(RET, b"\xf4")
        stub_security_cookie(uc)
        uc.mem_write(TREE_INSERT, b"\xc2\x0c\x00")  # std::set insert (allocates): the tree is built by the test
        uc.mem_write(TREE_ERASE, b"\xc2\x04\x00")  # the tree release (thiscall, 1 arg; recursive): recorded
        uc.hook_add(UC_HOOK_CODE, crt_memset, begin=CRT_MEMSET, end=CRT_MEMSET)
        self.exports = {}
        self.fake_n = 0
        uc.mem_write(REGISTER, b"\xff\x25" + struct.pack("<I", FAKE_PTR))  # Register: jmp [FAKE_PTR]
        if mode != "stock":
            self._load_dll()
            self._patch(mode)

    def _load_dll(self):
        self.exports = load_dll(self.uc)
        self.uc.mem_write(self.exports["Rs7_NeckLog"], b"\xc3")
        self.uc.mem_write(self.exports["g_rs7_fn_spawn"], struct.pack("<I", SPAWN))
        self.uc.mem_write(self.exports["g_rs7_fn_adv3"], struct.pack("<I", 0x7AA880))

    def _patch(self, mode: str):
        self.mids = 0
        text, base = game_text()
        for s in site_list():
            if mode == "native" and s.nextgrid != "native":
                continue
            ok, msg, va, target = sitecheck.check_site(text, base, s)
            assert ok, f"{s.id}: {msg}"
            if s.kind == "bytes":
                self.uc.mem_write(va, s.write)
                continue
            if s.id in MID_CALLBACKS:  # NG1 / NG9: the mid-hook entry (built before the site is overwritten)
                out = FAKE + 0xF0000 + 0x100 * self.mids
                stub = build_mid_entry(self.uc, self.exports, s, va, target, out, TOP - 0x4000, RET)
                self.mids += 1
            else:
                stub = self.exports[f"Rs7_Stub_{s.id}"]
            self.uc.mem_write(va, b"\xe9" + struct.pack("<i", stub - (va + 5)) + b"\x90" * (s.length - 5))
            self.uc.mem_write(self.exports[f"g_rs7_resume_{s.id}"], struct.pack("<I", va + s.length))
            if f"g_rs7_target_{s.id}" in self.exports:
                self.uc.mem_write(self.exports[f"g_rs7_target_{s.id}"], struct.pack("<I", target))

    def read(self, va, n):
        return bytes(self.uc.mem_read(va, n))

    def write(self, va, data):
        self.uc.mem_write(va, data)

    def reg(self, r):
        return self.uc.reg_read(r)

    def call(self, entry, args=(), esi=0, edi=0, ecx=0, eax=0, limit=2_000_000):
        """[esp] = RET, then the stack arguments."""
        sp = STACK + 0x8000
        self.write(sp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a) for a in args))
        for r, v in (
            (UC_X86_REG_ESP, sp),
            (UC_X86_REG_ESI, esi),
            (UC_X86_REG_EDI, edi),
            (UC_X86_REG_ECX, ecx),
            (UC_X86_REG_EAX, eax),
        ):
            self.uc.reg_write(r, v)
        self.uc.emu_start(entry, RET, count=limit)

    # --- the grid functions ----------------------------------------------------------------------------------
    def init(self, string_count: int, frets: int, fill=0xAA):
        self.write(GRID - 0x100, bytes([fill]) * 0x1400)
        self.call(INIT, (frets,), esi=GRID, eax=string_count)

    def set_cell(self, s, f, nt, t):
        """Rnd_NextNoteGrid_Set from 0x7AA163 (right after the tree insert, `fld [ebp+14h]`) to its `ret 10h`."""
        ebp = STACK + 0x10000
        self.write(ebp - 8, struct.pack("<IIIIIIII", 0x2222, 0x7777, 0x1111, RET, s, f, nt, f2u(t)))
        for r, v in (
            (UC_X86_REG_EBP, ebp),
            (UC_X86_REG_ESP, ebp - 8),
            (UC_X86_REG_ESI, GRID),
            (UC_X86_REG_EDI, ebp - 4),
            (UC_X86_REG_EBX, 0x3333),
        ):
            self.uc.reg_write(r, v)
        self.uc.emu_start(0x7AA163, RET, count=10000)
        assert self.reg(UC_X86_REG_ESP) == ebp + 0x18 and self.reg(UC_X86_REG_EBP) == 0x1111

    def prune(self):
        self.call(PRUNE, edi=GRID)

    def set_tree(self, times):
        """A right-leaning chain of set nodes under the header node G+0D04h (no times: the empty set Init builds)."""
        if not times:
            return
        head = GRID + 0xD04
        nodes = [NODES + i * 0x20 for i in range(len(times))]
        blob = bytearray()
        for i, t in enumerate(times):
            parent = head if i == 0 else nodes[i - 1]
            right = nodes[i + 1] if i + 1 < len(times) else 0
            blob += struct.pack("<IIIIf", 0, parent, 0, right, t) + bytes(12)
        self.write(NODES, bytes(blob))
        self.write(head + 4, struct.pack("<I", nodes[0]))
        self.write(head + 8, struct.pack("<I", nodes[0]))
        self.write(head + 0xC, struct.pack("<I", nodes[-1]))
        self.write(head + 0x10, struct.pack("<I", len(times)))


def time_base(mode):
    return ng.T_NATIVE if mode == "native" else ng.T_STOCK


def u32s(data):
    return struct.unpack(f"<{len(data) // 4}I", data)


def cells_equal(a: Machine, b: Machine, rows=range(6), cols=CELLS):
    """The pointer block 0..A90 and the time rows of `rows` agree between two machines (each at its own base)."""
    assert a.read(GRID, 0xA90) == b.read(GRID, 0xA90)
    for s in rows:
        ta = a.read(GRID + time_base(a.mode) + s * ROW, cols * 4)
        tb = b.read(GRID + time_base(b.mode) + s * ROW, cols * 4)
        assert ta == tb, f"time row {s}"


def fake_register(sets, tree_times, at):
    """Machine code for 0x7A5D50 (`ret 8`, stack: time, grid): builds the tree under G+0D04h and feeds Set."""
    nodes = [NODES + i * 0x20 for i in range(len(tree_times))]
    blob = bytearray()
    for i, t in enumerate(tree_times):
        right = nodes[i + 1] if i + 1 < len(tree_times) else 0
        blob += struct.pack("<IIIIf", 0, 0, 0, right, t) + bytes(12)
    c = bytearray(b"\x56\x8b\x74\x24\x0c")  # push esi; mov esi,[esp+0Ch] (the grid)
    if tree_times:
        c += (
            b"\x8d\x86\x04\x0d\x00\x00" + b"\xa3" + struct.pack("<I", nodes[0] + 4)
        )  # lea eax,[esi+0D04h]; mov [node0+4],eax
        for off, val in ((0xD08, nodes[0]), (0xD0C, nodes[0]), (0xD10, nodes[-1]), (0xD14, len(tree_times))):
            c += b"\xc7\x86" + struct.pack("<II", off, val)  # mov dword [esi+off],val
    for s, f, nt, t in sets:
        c += b"\x68" + struct.pack("<I", f2u(t)) + b"\x68" + struct.pack("<I", nt)
        c += b"\x68" + struct.pack("<I", f) + b"\x68" + struct.pack("<I", s)
        c += b"\xe8" + struct.pack("<i", SET - (at + len(c) + 5))
    c += b"\x5e\xc2\x08\x00"  # pop esi; ret 8
    return bytes(c), bytes(blob)


def run_update(m: Machine, sets, tree_times, strings=6, frets=24, dt=0.016):
    """Runs 0x7E4870 once. Returns (spawn calls, final registers, the bytes above the frame, esp at entry, cookie)."""
    # fresh code address per run: Unicorn does not invalidate translated blocks of rewritten code
    at = FAKE + 0x800 * m.fake_n
    m.fake_n += 1
    code, blob = fake_register(sets, tree_times, at)
    m.write(at, code)
    m.write(FAKE_PTR, struct.pack("<I", at))
    m.write(NODES, blob)
    m.write(SPAWN, b"\xc2\x18\x00")
    for p in sorted({n for _, _, n, _ in sets}):
        k = (p - NOTE_BASE) // 0x100
        flags = bytes([0, 0]) if k % 5 == 4 else bytes([0, 1]) if k % 3 == 0 else bytes([1, 0])
        m.write(p, bytes(0x51) + flags + bytes(0x80 - 0x53))
    info, ptra, lst = SESS + 0x800, SESS + 0x810, SESS + 0x900
    m.write(SESS, bytes(0x600))
    m.write(SESS + 0x37C, struct.pack("<I", ptra))
    m.write(ptra + 0x24, struct.pack("<I", info))
    m.write(info + 0xB, bytes([strings, frets]))
    m.write(SESS + 0x41C, struct.pack("<II", lst, lst + 4))
    m.write(lst, struct.pack("<I", note(900)))
    m.write(SESS + 0x3F0, struct.pack("<ff", 0.5, 0.25))
    m.write(STACK, b"\xcd" * STACK_SIZE)
    esp = TOP
    m.write(esp, struct.pack("<IIf", RET, SESS, dt))
    for r, v in (
        (UC_X86_REG_ESP, esp),
        (UC_X86_REG_EBP, 0x2468),
        (UC_X86_REG_EBX, 0xB0B0),
        (UC_X86_REG_ESI, 0x5151),
        (UC_X86_REG_EDI, 0xD1D1),
    ):
        m.uc.reg_write(r, v)
    calls, erase = [], []
    h = m.uc.hook_add(
        UC_HOOK_CODE,
        lambda uc, a, s, u: calls.append(struct.unpack("<6I", bytes(uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 24)))),
        begin=SPAWN,
        end=SPAWN,
    )
    h2 = m.uc.hook_add(
        UC_HOOK_CODE,
        lambda uc, a, s, u: erase.append(
            (
                uc.reg_read(UC_X86_REG_ECX),
                struct.unpack("<I", bytes(uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 4)))[0],
            )
        ),
        begin=TREE_ERASE,
        end=TREE_ERASE,
    )
    try:
        m.uc.emu_start(UPDATE, RET, count=2_000_000)
    finally:
        m.uc.hook_del(h)
        m.uc.hook_del(h2)
    regs = tuple(m.reg(r) for r in (UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI))
    # [ebp-4] (ebp = esp-4) holds the /GS cookie ^ ebp: an overrun of the grid would have changed it
    cookie = struct.unpack("<I", m.read(esp - 8, 4))[0] == struct.unpack("<I", m.read(0x125B240, 4))[0] ^ (esp - 4)
    return calls, regs, m.read(esp + 4, 0x40), esp, cookie, erase
